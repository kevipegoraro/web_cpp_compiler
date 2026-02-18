# web_cpp_compiler
# `execute` Method Documentation

## Overview

`execute(const std::string& code)` is the central control routine of the interpreter. It parses and executes a multi-line script written in the custom language supported by the `Interpreter` class.

The method:

* Processes the script line by line
* Identifies control-flow constructs (`loop`, `if`)
* Delegates simple statements to `runLine`
* Supports nested blocks via recursion

---

## Method Signature

```cpp
void execute(const std::string& code);
```

### Parameters

| Name   | Type                 | Description                                                      |
| ------ | -------------------- | ---------------------------------------------------------------- |
| `code` | `const std::string&` | Full script containing one or more lines of interpreter commands |

---

## Execution Model

1. Convert the full script into a stream:

   ```cpp
   std::istringstream stream(code);
   ```

2. Read the script line by line:

   ```cpp
   while (std::getline(stream, line))
   ```

3. For each non-empty line:

   * Extract the first token (command)
   * Dispatch to:

     * `loop` handler
     * `if` handler
     * `runLine()` for all other commands

---

# Control Flow Handling

## 1. `loop` Command

### Syntax

```
loop <variable>:<count> (
    <block>
)
```

### Example

```
loop i:3 (
    print i
)
```

### Behavior

1. Parse loop variable and iteration count.
2. Validate opening parenthesis.
3. Extract loop body using `readBlock()`.
4. Execute the block `count` times.
5. Assign the loop index to the loop variable on each iteration.
6. Invoke `execute(block)` recursively.

### Implementation Details

* The loop header is parsed as `<var>:<count>`.
* `std::stoi` converts the iteration count.
* The block is read until matching parentheses close.
* Recursion enables nested loops and conditionals.

---

## 2. `if` Command

### Syntax

```
if <condition> (
    <block>
)
```

### Example

```
if x > 5 (
    print "Large"
)
```

### Behavior

1. Extract the condition expression.
2. Trim whitespace and remove trailing `(` if present.
3. Extract block using `readBlock()`.
4. Evaluate condition using `evaluateCondition()`.
5. Execute block only if condition evaluates to `true`.

### Supported Operators

* `>`
* `<`
* `>=`
* `<=`
* `==`
* `!=`

---

# Delegation to `runLine`

If a line does not begin with `loop` or `if`, it is passed to:

```cpp
runLine(line);
```

`runLine` handles single-line commands including:

* `print`
* `set`
* `add`
* `sub`
* `mult`
* `div`

---

# Helper Functions Used by `execute`

## `readBlock(std::istringstream& stream)`

### Purpose

Extracts all lines inside a parenthesized block.

### Mechanism

* Tracks nesting depth using a counter.
* Increments depth for each `(`.
* Decrements depth for each `)`.
* Stops when depth reaches zero.

### Result

Returns a string containing the block contents without outer parentheses.

This allows proper handling of nested blocks.

---

## `evaluateCondition(const std::string& condition)`

### Purpose

Evaluates simple binary comparisons.

### Expected Format

```
<left> <operator> <right>
```

### Behavior

1. Parses left operand, operator, and right operand.
2. Resolves operands as:

   * Variable values (if present in `variables`)
   * Integer literals (via `std::stoi`)
3. Performs comparison.
4. Returns boolean result.

Invalid operators trigger an error message and return `false`.

---

# Recursion Model

`execute` is recursively invoked when:

* A `loop` executes its block.
* An `if` condition evaluates to true.

This enables:

* Nested loops
* Nested conditionals
* Arbitrary block depth

Example:

```
loop i:3 (
    if i > 1 (
        print i
    )
)
```

Each nested block is handled by a recursive call to `execute`.

---

# Variable Storage

Variables are stored in:

```cpp
std::map<std::string, int> variables;
```

All commands operate on this shared state.

Loop variables are assigned per iteration and persist after execution.

---

# Execution Flow Summary

```
main()
    → execute(full_script)
        → read line
            → if loop → readBlock → recursive execute
            → if if   → evaluateCondition → recursive execute
            → else    → runLine
```

---

# Design Characteristics

* Line-oriented parsing
* Immediate execution (no AST construction)
* Recursive block evaluation
* Minimal expression grammar (binary comparisons only)
* Shared mutable variable state

---

# Limitations

* No support for `else`
* No complex expressions (e.g., arithmetic in conditions)
* No scoped variables (all variables are global)
* No error recovery beyond simple reporting
* No type system (integers only)

---

# Conclusion

The `execute` method functions as the interpreter’s primary dispatcher and control-flow engine. It parses the script, delegates simple statements, and recursively evaluates structured blocks. The design prioritizes simplicity and direct execution over formal parsing or compilation stages.
