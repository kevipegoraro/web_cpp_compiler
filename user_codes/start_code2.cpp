#include <iostream>     // For std::cout, std::endl
#include <sstream>      // For string streams (parsing lines)
#include <string>       // For std::string
#include <map>          // For storing variables
#include <fstream>      // For reading files

// ===============================
// Simple Interpreter Class
// ===============================
class Interpreter {
private:

    // Map to store variables
    // Example:
    // set x = 5
    // This will store: variables["x"] = 5
    std::map<std::string, int> variables;

public:

    // ============================================
    // Execute full script (multiple lines of code)
    // ============================================
void execute(const std::string& code) {

    std::istringstream stream(code);
    std::string line;

    while (std::getline(stream, line)) {

        if (line.empty())
            continue;

        std::istringstream ss(line);
        std::string command;
        ss >> command;

        // =========================
        // LOOP COMMAND
        // =========================
        if (command == "loop") {

            std::string varAndCount;
            ss >> varAndCount;

            // Example: i:10
            size_t colonPos = varAndCount.find(':');

            std::string var = varAndCount.substr(0, colonPos);
            int count = std::stoi(varAndCount.substr(colonPos + 1));

            // Expect "(" at end of line
            std::string openParen;
            ss >> openParen;

            if (openParen != "(") {
                std::cout << "Syntax error: expected (\n";
                continue;
            }
            
            // Calculate block separately to support nested loops and ifs
            std::string block = readBlock(stream);


            // Execute loop
            for (int i = 0; i < count; i++) {

                variables[var] = i;
                execute(block);  // Recursive call
            }
        }

                // =========================
        // IF COMMAND
        // =========================
        else if (command == "if") {

            // Get rest of line after "if"
            std::string condition;
            std::getline(ss, condition);

            // Remove trailing "("
            if (!condition.empty() && condition.back() == '(')
                condition.pop_back();

            // Trim spaces
            condition.erase(0, condition.find_first_not_of(" "));
            condition.erase(condition.find_last_not_of(" ") + 1);

            // Expect "("
            std::string openParen;
            ss >> openParen;

            // If "(" is not at the end of line, it means condition is separated by space
            std::string block = readBlock(stream);


            if (evaluateCondition(condition)) {
                execute(block);
            }
        }


        else {
            runLine(line);
        }
    }
}


private:

    // ============================================
    // Execute one single line of code
    // ============================================
    void runLine(const std::string& line) {


        // Ignore empty lines
        if (line.empty())
            return;

        // If line starts with "comment", ignore it
        if (line.rfind("comment", 0) == 0)
            return;


        // Create a stream for parsing the line
        std::istringstream ss(line);

        std::string command;


        // Read the first word (the command)
        ss >> command;

        // =========================
        // PRINT COMMAND
        // =========================
        if (command == "print") {

            // Get everything after the word "print"
            std::string restOfLine;
            std::getline(ss, restOfLine);

            // Remove leading space (because getline keeps it)
            if (!restOfLine.empty() && restOfLine[0] == ' ')
                restOfLine.erase(0, 1);

            // ---------------------------------------
            // Case 1: If it's a quoted string
            // Example:
            // print "Hello World"
            // ---------------------------------------
            if (restOfLine.size() >= 2 &&
                restOfLine.front() == '"' &&
                restOfLine.back() == '"') {

                // Remove the quotes
                std::string text =
                    restOfLine.substr(1, restOfLine.size() - 2);

                std::cout << text << std::endl;
            }

            // ---------------------------------------
            // Case 2: If it's a variable name
            // Example:
            // print x
            // ---------------------------------------
            else {

                if (variables.count(restOfLine)) {
                    std::cout << variables[restOfLine] << std::endl;
                }
                else {
                    // If not a variable, just print as-is
                    std::cout << restOfLine << std::endl;
                }
            }
        }

        // =========================
        // SET COMMAND
        // =========================
        // Example:
        // set x = 5
        else if (command == "set") {

            std::string var;
            std::string valueToken;

            ss >> var >> valueToken;

            // Case 1: set x = 5
            if (valueToken == "=") {
                ss >> valueToken;
            }

            // Now valueToken can be:
            // - a number
            // - a variable name

            // Check if it's a number
            if (std::isdigit(valueToken[0]) || 
                (valueToken[0] == '-' && valueToken.size() > 1)) {

                variables[var] = std::stoi(valueToken);
            }
            else {
                // Otherwise treat it as variable
                if (variables.count(valueToken)) {
                    variables[var] = variables[valueToken];
                }
                else {
                    std::cout << "Error: variable '" 
                            << valueToken 
                            << "' not found\n";
                }
            }
        }


        // =========================
        // ADD COMMAND
        // =========================
        // Example:
        // add x 3
        else if (command == "add") {

            std::string var;
            int value;

            ss >> var >> value;

            // Only add if variable exists
            if (variables.count(var)) {
                variables[var] += value;
            }
            else {
                std::cout << "Error: variable '" << var << "' not found\n";
            }
        }

        // =========================
        // SUB COMMAND
        // Example:
        // sub x 3
        // =========================
        else if (command == "sub") {

            std::string var;
            int value;

            ss >> var >> value;

            if (variables.count(var)) {
                variables[var] -= value;
            }
            else {
                std::cout << "Error: variable '" << var << "' not found\n";
            }
        }

        // =========================
        // MULT COMMAND
        // Example:
        // mult x 3
        // =========================
        else if (command == "mult") {

            std::string var;
            int value;

            ss >> var >> value;

            if (variables.count(var)) {
                variables[var] *= value;
            }
            else {
                std::cout << "Error: variable '" << var << "' not found\n";
            }
        }

        // =========================
        // DIV COMMAND
        // Example:
        // div x 2
        // =========================
        else if (command == "div") {

            std::string var;
            int value;

            ss >> var >> value;

            if (!variables.count(var)) {
                std::cout << "Error: variable '" << var << "' not found\n";
                return;
            }

            if (value == 0) {
                std::cout << "Error: division by zero\n";
                return;
            }

            variables[var] /= value;
        }

        // =========================
        // UNKNOWN COMMAND
        // =========================
        else {
            std::cout << "Unknown command: " << command << std::endl;
        }
    }
    
    bool evaluateCondition(const std::string& condition) {

        std::istringstream ss(condition);

        std::string left, op, right;
        ss >> left >> op >> right;

        int leftVal = 0;
        int rightVal = 0;

        // Left value
        if (variables.count(left))
            leftVal = variables[left];
        else
            leftVal = std::stoi(left);

        // Right value
        if (variables.count(right))
            rightVal = variables[right];
        else
            rightVal = std::stoi(right);

        // Comparison
        if (op == ">")  return leftVal > rightVal;
        if (op == "<")  return leftVal < rightVal;
        if (op == ">=") return leftVal >= rightVal;
        if (op == "<=") return leftVal <= rightVal;
        if (op == "==") return leftVal == rightVal;
        if (op == "!=") return leftVal != rightVal;

        std::cout << "Invalid operator in condition\n";
        return false;
    }

    std::string readBlock(std::istringstream& stream) {
    std::string block;
    std::string line;
    int depth = 1;

    while (std::getline(stream, line)) {

        for (char c : line) {
            if (c == '(') depth++;
            else if (c == ')') depth--;
        }

        if (depth == 0)
            break;

        block += line + "\n";
    }

    return block;
}



};

// ============================================
// MAIN FUNCTION
// ============================================
int main() {

    std::stringstream buffer;
    buffer << std::cin.rdbuf();

    Interpreter interpreter;
    interpreter.execute(buffer.str());

    return 0;
}