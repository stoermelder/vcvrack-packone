# Array Implementation for Elk JavaScript Engine

This document describes the array handling implementation added to the Elk JavaScript interpreter.

## Features

### Array Literal Syntax
Arrays can be created using standard JavaScript square bracket syntax:

```javascript
let empty = [];
let numbers = [1, 2, 3, 4, 5];
let mixed = [1, "hello", true, null];
let nested = [[1, 2], [3, 4]];
```

### Array Indexing
Array elements can be accessed and modified using square bracket notation:

```javascript
let arr = [10, 20, 30];
arr[0];      // Returns 10
arr[1] = 25; // Sets second element to 25
arr[2];      // Returns 30
```

### Dynamic Indexing
Indexes can be expressions that evaluate to numbers:

```javascript
let arr = [1, 2, 3];
let i = 1;
arr[i];     // Returns 2
arr[i + 1]; // Returns 3
```

### Array Length Property
Arrays automatically have a `length` property:

```javascript
let arr = [1, 2, 3];
arr.length; // Returns 3
```

## Implementation Details

### Internal Representation
Arrays are implemented as objects with:
- Numeric string keys ("0", "1", "2", etc.) for element storage
- A "length" property that stores the array length

For example, `[1, 2, 3]` is internally represented as:
```
{
  "0": 1,
  "1": 2,
  "2": 3,
  "length": 3
}
```

### Token Support
Two new tokens were added:
- `TOK_LBRACKET` for `[`
- `TOK_RBRACKET` for `]`

### Parsing
- **Array Literals**: Handled by `js_array_literal()` function
- **Array Indexing**: Handled in `js_call_dot()` function, which now supports dot notation (`.`), function calls `()`, and bracket indexing `[]`

### Index Conversion
When using bracket notation `arr[index]`:
- Numeric indexes are converted to string keys
- String indexes are used directly
- The lookup uses the existing object property mechanism

## Testing

To test the array implementation, compile and run the test file:

```bash
gcc -o test_array test_array.c -lm
./test_array
```

## Examples

```javascript
// Create an array
let fruits = ["apple", "banana", "orange"];

// Access elements
fruits[0];  // "apple"
fruits[1];  // "banana"

// Modify elements
fruits[1] = "grape";
fruits[1];  // "grape"

// Get length
fruits.length;  // 3

// Nested arrays
let matrix = [[1, 2], [3, 4]];
matrix[0][0];  // 1
matrix[1][1];  // 4

// Dynamic indexing
let i = 0;
fruits[i];  // "apple"
```

## Limitations

- The `length` property is set when the array is created and is **automatically updated** when numeric indices beyond the current length are assigned (e.g. assigning to index `i` sets `length = max(length, i + 1)`).
- Assigning to non-numeric or negative property names (e.g. `arr[-1] = 2` or `arr['foo'] = 2`) does **not** affect `length`.
- Setting `length` directly (e.g. `arr.length = 1`) **results in a runtime error** ("cannot set array length").
- No built-in array methods (push, pop, slice, etc.) are implemented
- Arrays use the same object structure as regular objects, so they can have non-numeric properties

## Future Enhancements

Possible improvements:
1. Array methods (push, pop, shift, unshift, slice, splice, etc.)
2. Array iteration methods (forEach, map, filter, reduce)
3. Sparse array support
4. Array.isArray() method
