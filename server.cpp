#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <regex>
#include <nlohmann/json.hpp>
using json = nlohmann::json;


#define PORT 8080

// URL decode (basic)
static std::string url_decode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) {
            auto hex = s.substr(i + 1, 2);
            char c = (char)std::strtol(hex.c_str(), nullptr, 16);
            out.push_back(c);
            i += 2;
        } else if (s[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

static void split_path_query(const std::string& target, std::string& path, std::string& query) {
    auto q = target.find('?');
    if (q == std::string::npos) { path = target; query = ""; }
    else { path = target.substr(0, q); query = target.substr(q + 1); }
}

static std::unordered_map<std::string, std::string> parse_query(const std::string& q) {
    std::unordered_map<std::string, std::string> m;
    size_t i = 0;
    while (i < q.size()) {
        size_t amp = q.find('&', i);
        std::string part = q.substr(i, amp == std::string::npos ? q.size() - i : amp - i);
        size_t eq = part.find('=');
        std::string k = url_decode(eq == std::string::npos ? part : part.substr(0, eq));
        std::string v = url_decode(eq == std::string::npos ? "" : part.substr(eq + 1));
        if (!k.empty()) m[k] = v;
        if (amp == std::string::npos) break;
        i = amp + 1;
    }
    return m;
}

// Only allow safe filenames like "star_code.cpp" or "test-1.cpp"
static std::optional<std::string> sanitize_cpp_filename(const std::string& name) {
    if (name.size() > 80) return std::nullopt;
    static const std::regex ok(R"(^[A-Za-z0-9_.-]+\.cpp$)");
    if (!std::regex_match(name, ok)) return std::nullopt;
    if (name.find("..") != std::string::npos) return std::nullopt;
    return name;
}



// ------------------------- Small HTTP helpers -------------------------

struct HttpRequest {
    std::string method;
    std::string path;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) b--;
    return s.substr(a, b - a);
}

static std::string to_lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static std::optional<HttpRequest> parse_http_request(const std::string& raw) {
    auto header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos) return std::nullopt;

    std::string header_part = raw.substr(0, header_end);
    std::string body_part = raw.substr(header_end + 4);

    std::istringstream hs(header_part);
    std::string request_line;
    if (!std::getline(hs, request_line)) return std::nullopt;
    if (!request_line.empty() && request_line.back() == '\r') request_line.pop_back();

    std::istringstream rl(request_line);
    HttpRequest req;
    std::string version;
    rl >> req.method >> req.path >> version;
    if (req.method.empty() || req.path.empty()) return std::nullopt;

    std::string line;
    while (std::getline(hs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = to_lower(trim(line.substr(0, colon)));
        std::string val = trim(line.substr(colon + 1));
        req.headers[key] = val;
    }

    req.body = body_part;
    return req;
}

static std::string http_response(int status_code,
                                 const std::string& status_text,
                                 const std::string& content_type,
                                 const std::string& body) {
    std::ostringstream out;
    out << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";
    out << "Content-Type: " << content_type << "\r\n";
    out << "Content-Length: " << body.size() << "\r\n";
    out << "Connection: close\r\n";
    out << "\r\n";
    out << body;
    return out.str();
}

static bool send_all(int fd, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = send(fd, data.data() + sent, data.size() - sent, 0);
        if (n <= 0) return false;
        sent += (size_t)n;
    }
    return true;
}

static std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Reads until headers are complete, then reads Content-Length bytes (if present).
static std::string read_http_from_socket(int fd) {
    constexpr size_t MAX_REQ = 512 * 1024; // 512 KB limit for safety
    std::string data;
    char buf[4096];

    // Read until we have headers
    while (data.find("\r\n\r\n") == std::string::npos) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        data.append(buf, buf + n);
        if (data.size() > MAX_REQ) break;
    }

    auto header_end = data.find("\r\n\r\n");
    if (header_end == std::string::npos) return data;

    // Parse Content-Length (if any)
    auto maybe_req = parse_http_request(data);
    if (!maybe_req) return data;

    auto it = maybe_req->headers.find("content-length");
    if (it == maybe_req->headers.end()) return data;

    long long content_length = 0;
    try {
        content_length = std::stoll(it->second);
    } catch (...) {
        return data;
    }
    if (content_length < 0 || content_length > (long long)MAX_REQ) return data;

    std::string body = data.substr(header_end + 4);
    while ((long long)body.size() < content_length) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        body.append(buf, buf + n);
        if (body.size() > MAX_REQ) break;
    }

    return data.substr(0, header_end + 4) + body;
}

// ------------------------- Sandboxed-ish runner -------------------------

static void apply_run_limits() {
    // CPU time: 2 seconds
    struct rlimit rl_cpu {2, 2};
    setrlimit(RLIMIT_CPU, &rl_cpu);

    // Address space: 256 MB (rough memory cap)
    struct rlimit rl_as {256ULL * 1024ULL * 1024ULL, 256ULL * 1024ULL * 1024ULL};
    setrlimit(RLIMIT_AS, &rl_as);

    // Output file size: 1 MB
    struct rlimit rl_fsize {1ULL * 1024ULL * 1024ULL, 1ULL * 1024ULL * 1024ULL};
    setrlimit(RLIMIT_FSIZE, &rl_fsize);

    // Open files: 64
    struct rlimit rl_nofile {64, 64};
    setrlimit(RLIMIT_NOFILE, &rl_nofile);
}

struct ProcResult {
    int exit_code = -1;
    bool timed_out = false;
    std::string output; // combined stdout+stderr
};
static ProcResult run_process_capture(
    const std::vector<std::string>& args,
    const std::string& input,
    int timeout_ms,
    bool limit_resources)
{
    ProcResult res;

    int stdout_pipe[2];
    int stdin_pipe[2];

    if (pipe(stdout_pipe) != 0 || pipe(stdin_pipe) != 0) {
        res.output = "Internal error: pipe() failed.\n";
        return res;
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stdin_pipe[0]);  close(stdin_pipe[1]);
        res.output = "Internal error: fork() failed.\n";
        return res;
    }

    if (pid == 0) {
        // ---------------- CHILD ----------------
        setpgid(0, 0);

        if (limit_resources)
            apply_run_limits();

        // Redirect stdout and stderr
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stdout_pipe[1], STDERR_FILENO);

        // Redirect stdin
        dup2(stdin_pipe[0], STDIN_FILENO);

        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);

        std::vector<char*> cargs;
        for (const auto& s : args)
            cargs.push_back(const_cast<char*>(s.c_str()));
        cargs.push_back(nullptr);

        execvp(cargs[0], cargs.data());

        std::cerr << "Internal error: exec failed.\n";
        _exit(127);
    }

    // ---------------- PARENT ----------------
    close(stdout_pipe[1]);
    close(stdin_pipe[0]);

    // Write input to child
    if (!input.empty()) {
        write(stdin_pipe[1], input.c_str(), input.size());
    }
    close(stdin_pipe[1]); // Important: signal EOF to child

    auto start = std::chrono::steady_clock::now();
    bool finished = false;

    while (!finished) {

        // Drain output (non-blocking)
        char buf[4096];
        ssize_t n = recv(stdout_pipe[0], buf, sizeof(buf), MSG_DONTWAIT);
        while (n > 0) {
            res.output.append(buf, buf + n);
            n = recv(stdout_pipe[0], buf, sizeof(buf), MSG_DONTWAIT);
        }

        int status = 0;
        pid_t w = waitpid(pid, &status, WNOHANG);

        if (w == pid) {
            finished = true;

            if (WIFEXITED(status))
                res.exit_code = WEXITSTATUS(status);
            else if (WIFSIGNALED(status))
                res.exit_code = 128 + WTERMSIG(status);

            break;
        }

        auto now = std::chrono::steady_clock::now();
        int elapsed_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();

        if (elapsed_ms > timeout_ms) {
            res.timed_out = true;
            kill(-pid, SIGKILL); // kill whole process group
            waitpid(pid, nullptr, 0);
            res.exit_code = 124;
            finished = true;
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Final drain
    for (;;) {
        char buf[4096];
        ssize_t n = read(stdout_pipe[0], buf, sizeof(buf));
        if (n <= 0) break;
        res.output.append(buf, buf + n);
    }

    close(stdout_pipe[0]);

    return res;
}

static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if ((unsigned char)c < 0x20) {
                    // Control chars -> drop
                } else out += c;
        }
    }
    return out;
}

static std::string handle_run_cpp(const std::string& code,
                                  const std::string& input)
{
    namespace fs = std::filesystem;

    fs::create_directories("user_codes");

    std::string source_path = "user_codes/temp.cpp";
    std::string binary_path = "user_codes/temp.out";

    // 1️⃣ Write source file
    {
        std::ofstream out(source_path);
        if (!out) {
            return R"({"ok":false,"error":"Failed to write source file"})";
        }
        out << code;
    }

    // 2️⃣ Compile
    ProcResult compile = run_process_capture(
        {"g++", source_path, "-std=c++17", "-O2", "-o", binary_path},
        "",
        5000,
        false
    );

    if (compile.exit_code != 0) {
        return std::string("{\"ok\":false,\"stage\":\"compile\",\"output\":\"")
            + json_escape(compile.output) + "\"}";
    }

    // 3️⃣ Run
    ProcResult run = run_process_capture(
        {binary_path},
        input,
        2000,
        true   // apply resource limits
    );

    std::string json = "{";
    json += "\"ok\":true,";
    json += "\"exit_code\":" + std::to_string(run.exit_code) + ",";
    json += "\"timed_out\":" + std::string(run.timed_out ? "true" : "false") + ",";
    json += "\"output\":\"" + json_escape(run.output) + "\"";
    json += "}";

    return json;
}




// ------------------------- Main server -------------------------

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(PORT);
    // Bind to localhost only for safety
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 16) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    std::cout << "Server running on http://127.0.0.1:" << PORT << "\n";

    while (true) {
        sockaddr_in client{};
        socklen_t client_len = sizeof(client);
        int client_fd = accept(server_fd, (struct sockaddr*)&client, &client_len);
        if (client_fd < 0) continue;

        std::string raw = read_http_from_socket(client_fd);
        auto req_opt = parse_http_request(raw);

        if (!req_opt) {
            auto resp = http_response(400, "Bad Request", "text/plain; charset=utf-8",
                                      "Bad Request\n");
            send_all(client_fd, resp);
            close(client_fd);
            continue;
        }

        HttpRequest req = *req_opt;

        std::string path, query;
        split_path_query(req.path, path, query);
        auto params = parse_query(query);

        // ensure folder exists
        std::filesystem::create_directories("user_codes");


        if (req.method == "GET" && (req.path == "/" || req.path == "/index.html")) {
            std::string content = read_file("public/index.html");
            if (content.empty()) {
                auto resp = http_response(404, "Not Found", "text/plain; charset=utf-8",
                                          "public/index.html not found.\n");
                send_all(client_fd, resp);
            } else {
                auto resp = http_response(200, "OK", "text/html; charset=utf-8", content);
                send_all(client_fd, resp);
            }
        }
else if (req.method == "POST" && req.path == "/run") {
    try {
        auto j = json::parse(req.body);

        std::string code  = j.value("code", "");
        std::string input = j.value("input", "");

        if (code.empty()) {
            auto resp = http_response(400, "Bad Request",
                "application/json; charset=utf-8",
                R"({"ok":false,"error":"Missing 'code'"})");
            send_all(client_fd, resp);
            close(client_fd);
            continue;
        }

        std::string out_json = handle_run_cpp(code, input);

        auto resp = http_response(200, "OK",
            "application/json; charset=utf-8", out_json);
        send_all(client_fd, resp);
    }
    catch (const std::exception& e) {
        std::string err = std::string("{\"ok\":false,\"error\":\"Invalid JSON: ")
                        + json_escape(e.what()) + "\"}";
        auto resp = http_response(400, "Bad Request",
            "application/json; charset=utf-8", err);
        send_all(client_fd, resp);
    }
}

        else if (req.method == "GET" && path == "/load") {
            std::string name = params.count("name") ? params["name"] : "star_code.cpp";
            auto safe = sanitize_cpp_filename(name);
            if (!safe) {
                auto resp = http_response(400, "Bad Request", "text/plain; charset=utf-8",
                                        "Invalid filename. Use something like star_code.cpp\n");
                send_all(client_fd, resp);
            } else {
                std::string full = "user_codes/" + *safe;
                std::string content = read_file(full);
                if (content.empty() && !std::filesystem::exists(full)) {
                    auto resp = http_response(404, "Not Found", "text/plain; charset=utf-8",
                                            ("File not found: " + full + "\n"));
                    send_all(client_fd, resp);
                } else {
                    auto resp = http_response(200, "OK", "text/plain; charset=utf-8", content);
                    send_all(client_fd, resp);
                }
            }
        }
        
        
        else if (req.method == "POST" && path == "/save") {
            std::string name = params.count("name") ? params["name"] : "star_code.cpp";
            auto safe = sanitize_cpp_filename(name);
            if (!safe) {
                auto resp = http_response(400, "Bad Request", "application/json; charset=utf-8",
                                        R"({"ok":false,"error":"Invalid filename. Use something like star_code.cpp"})");
                send_all(client_fd, resp);
            } else {
                std::string full = "user_codes/" + *safe;
                std::ofstream out(full, std::ios::binary);
                if (!out) {
                    auto resp = http_response(500, "Internal Server Error", "application/json; charset=utf-8",
                                            R"({"ok":false,"error":"Failed to open file for writing."})");
                    send_all(client_fd, resp);
                } else {
                    out << req.body;
                    out.close();
                    std::string body = std::string("{\"ok\":true,\"savedAs\":\"") + json_escape(*safe) +
                                    "\",\"bytes\":" + std::to_string(req.body.size()) + "}";
                    auto resp = http_response(200, "OK", "application/json; charset=utf-8", body);
                    send_all(client_fd, resp);
                }
            }
        }

        else {
            auto resp = http_response(404, "Not Found", "text/plain; charset=utf-8",
                                      "Not Found\n");
            send_all(client_fd, resp);
        }

        close(client_fd);
    }

    close(server_fd);
    return 0;
}
