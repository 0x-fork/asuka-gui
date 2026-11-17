# invalid-character

A character that isn't part of the language here.

An error the compiler reports: the program doesn't build until it's fixed.

## Why

Outside strings and comments, a file holds names, numbers and Asuka's operators, in UTF-8. Anything else is a typo or text pasted from elsewhere.

## How to fix

Remove the character, or put the text in a string or a comment.

## Wrong

```asuka
let total = 1 $ 2
```

## Right

```asuka
let total = 1 + 2
```
