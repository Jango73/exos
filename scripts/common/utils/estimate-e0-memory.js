#!/usr/bin/env node

"use strict";

const fs = require("fs");
const path = require("path");

const MAX_VAR_NAME = 64;
const MAX_TOKEN_LENGTH = 128;
const SCRIPT_VAR_HASH_SIZE = 32;
const ROOT_BLOCK_INITIAL_CAPACITY = 16;
const ARRAY_INITIAL_CAPACITY = 4;
const OBJECT_INITIAL_CAPACITY = 4;
const FOR_MAX_ITERATIONS = 1000;

const TOKEN = {
    EOF: "eof",
    IDENTIFIER: "identifier",
    PATH: "path",
    NUMBER: "number",
    STRING: "string",
    OPERATOR: "operator",
    LPAREN: "(",
    RPAREN: ")",
    LBRACKET: "[",
    RBRACKET: "]",
    SEMICOLON: ";",
    COMMA: ",",
    COMPARISON: "comparison",
    LBRACE: "{",
    RBRACE: "}",
    IF: "if",
    ELSE: "else",
    FOR: "for",
    RETURN: "return",
    CONTINUE: "continue",
};

const AST = {
    ASSIGNMENT: "assignment",
    IF: "if",
    FOR: "for",
    BLOCK: "block",
    RETURN: "return",
    CONTINUE: "continue",
    EXPRESSION: "expression",
};

const VALUE = {
    STRING: "string",
    INTEGER: "integer",
    FLOAT: "float",
    ARRAY: "array",
    OBJECT: "object",
    HOST: "host-handle",
    UNKNOWN: "unknown",
};

function fail(message) {
    process.stderr.write(`${message}\n`);
    process.exit(1);
}

function isNumeric(ch) {
    return ch >= "0" && ch <= "9";
}

function isIdentifierStart(ch) {
    return ((ch >= "a" && ch <= "z") ||
        (ch >= "A" && ch <= "Z") ||
        ch === "_");
}

function isIdentifierPart(ch) {
    return isIdentifierStart(ch) || isNumeric(ch);
}

function truncateToken(value) {
    if (value.length >= MAX_TOKEN_LENGTH) {
        return value.slice(0, MAX_TOKEN_LENGTH - 1);
    }

    return value;
}

function capacityFromCount(count, initialCapacity) {
    let capacity = initialCapacity;
    while (count > capacity) {
        capacity *= 2;
    }
    return capacity;
}

function isIntegerNumber(value) {
    return Number.isFinite(value) && Math.trunc(value) === value;
}

function makeArchitectureModel(pointerSize) {
    const uintSize = pointerSize;
    const intSize = pointerSize;
    const boolSize = pointerSize;
    const enumSize = 4;
    const u32Size = 4;
    const f32Size = 4;
    const u64Size = 8;
    const objectHeaderSize = (uintSize * 2) + u64Size + pointerSize + pointerSize;
    const listSize = (pointerSize * 3) + uintSize + pointerSize + (pointerSize * 4) + pointerSize;
    const allocatorSize = pointerSize * 4;
    const scriptVarValueSize = pointerSize;
    const scriptArraySize = (pointerSize * 3) + u32Size + u32Size;
    const listNodeFieldsSize = objectHeaderSize + (pointerSize * 3);
    const scriptVariableSize = listNodeFieldsSize + pointerSize + MAX_VAR_NAME + enumSize + scriptVarValueSize + u32Size;
    const scriptScopeSize = pointerSize + (SCRIPT_VAR_HASH_SIZE * pointerSize) + u32Size + pointerSize + u32Size;
    const scriptVarTableSize = (SCRIPT_VAR_HASH_SIZE * pointerSize) + u32Size;
    const scriptCallbacksSize = pointerSize * 5;
    const scriptHostRegistrySize = pointerSize + (SCRIPT_VAR_HASH_SIZE * pointerSize) + u32Size;
    const scriptValueSize = enumSize + scriptVarValueSize + pointerSize + pointerSize + boolSize + pointerSize;
    const scriptObjectPropertySize = MAX_TOKEN_LENGTH + scriptValueSize;
    const scriptObjectSize = (pointerSize * 2) + u32Size + u32Size + u32Size;
    const scriptHostSymbolSize = listNodeFieldsSize + pointerSize + MAX_VAR_NAME + enumSize + pointerSize + pointerSize + pointerSize;
    const assignmentUnionSize =
        MAX_VAR_NAME +
        pointerSize +
        boolSize +
        u32Size +
        pointerSize +
        boolSize +
        pointerSize +
        MAX_TOKEN_LENGTH;
    const expressionUnionSize =
        enumSize +
        MAX_TOKEN_LENGTH +
        boolSize +
        intSize +
        f32Size +
        boolSize +
        boolSize +
        u32Size +
        pointerSize +
        pointerSize +
        boolSize +
        MAX_TOKEN_LENGTH +
        boolSize +
        pointerSize +
        pointerSize +
        u32Size +
        pointerSize +
        pointerSize +
        boolSize +
        pointerSize;
    const astNodeSize = pointerSize + enumSize + Math.max(
        assignmentUnionSize,
        pointerSize * 3,
        pointerSize * 4,
        pointerSize + u32Size + u32Size,
        pointerSize,
        expressionUnionSize
    ) + pointerSize;
    const scriptContextSize =
        scriptVarTableSize +
        scriptCallbacksSize +
        allocatorSize +
        enumSize +
        256 +
        boolSize +
        boolSize +
        boolSize +
        enumSize +
        scriptVarValueSize +
        pointerSize +
        pointerSize +
        scriptHostRegistrySize;

    return {
        pointerSize,
        uintSize,
        intSize,
        boolSize,
        enumSize,
        u32Size,
        f32Size,
        listSize,
        scriptContextSize,
        scriptScopeSize,
        scriptVariableSize,
        scriptArraySize,
        scriptObjectSize,
        scriptObjectPropertySize,
        scriptHostSymbolSize,
        astNodeSize,
        fixedInterpreterBytes:
            scriptContextSize +
            scriptScopeSize +
            (SCRIPT_VAR_HASH_SIZE * listSize) +
            (SCRIPT_VAR_HASH_SIZE * listSize),
    };
}

const ARCH_MODELS = {
    "x86-32": makeArchitectureModel(4),
    "x86-64": makeArchitectureModel(8),
};

class MemoryTracker {
    constructor() {
        this.currentBytes = 0;
        this.peakBytes = 0;
        this.nextId = 1;
        this.live = new Map();
        this.byTag = new Map();
    }

    allocate(size, tag) {
        if (size < 0) {
            throw new Error(`Invalid allocation size: ${size}`);
        }

        const id = this.nextId++;
        this.live.set(id, { size, tag });
        this.currentBytes += size;
        this.peakBytes = Math.max(this.peakBytes, this.currentBytes);
        this.byTag.set(tag, (this.byTag.get(tag) || 0) + size);
        return id;
    }

    free(id) {
        if (id == null) {
            return;
        }

        const allocation = this.live.get(id);
        if (!allocation) {
            return;
        }

        this.currentBytes -= allocation.size;
        this.live.delete(id);
    }
}

class Tokenizer {
    constructor(input) {
        this.input = input;
        this.position = 0;
        this.current = null;
        this.nextToken();
    }

    cloneToken() {
        return { ...this.current };
    }

    restore(position, token) {
        this.position = position;
        this.current = { ...token };
    }

    nextToken() {
        const input = this.input;

        while (this.position < input.length) {
            const ch = input[this.position];
            if (ch === " " || ch === "\t" || ch === "\n" || ch === "\r") {
                this.position++;
                continue;
            }
            break;
        }

        const position = this.position;
        if (position >= input.length) {
            this.current = { type: TOKEN.EOF, value: "", position };
            return;
        }

        const ch = input[position];

        if (isNumeric(ch)) {
            let end = position;
            while (end < input.length && (isNumeric(input[end]) || input[end] === ".")) {
                end++;
            }

            const raw = truncateToken(input.slice(position, end));
            const isInteger = !raw.includes(".");
            const numericValue = Number(raw);
            this.position = end;
            this.current = {
                type: TOKEN.NUMBER,
                value: raw,
                position,
                isInteger,
                integerValue: isInteger ? numericValue : Math.trunc(numericValue),
                floatValue: numericValue,
            };
            return;
        }

        if (isIdentifierStart(ch)) {
            let end = position;
            while (end < input.length && isIdentifierPart(input[end])) {
                end++;
            }

            const raw = truncateToken(input.slice(position, end));
            this.position = end;
            const type = {
                if: TOKEN.IF,
                else: TOKEN.ELSE,
                for: TOKEN.FOR,
                return: TOKEN.RETURN,
                continue: TOKEN.CONTINUE,
            }[raw] || TOKEN.IDENTIFIER;
            this.current = { type, value: raw, position };
            return;
        }

        if (ch === "\"" || ch === "'") {
            const quote = ch;
            let end = position + 1;
            let out = "";

            while (end < input.length) {
                const current = input[end];
                if (current === quote) {
                    end++;
                    break;
                }

                if (current === "\\") {
                    end++;
                    if (end >= input.length) {
                        out += "\\";
                        break;
                    }

                    const escaped = input[end];
                    const table = {
                        n: "\n",
                        r: "\r",
                        t: "\t",
                        "\\": "\\",
                        "'": "'",
                        "\"": "\"",
                    };
                    if (Object.prototype.hasOwnProperty.call(table, escaped)) {
                        out += table[escaped];
                    } else {
                        out += `\\${escaped}`;
                    }
                    end++;
                    if (out.length >= MAX_TOKEN_LENGTH - 1) {
                        out = out.slice(0, MAX_TOKEN_LENGTH - 1);
                    }
                    continue;
                }

                out += current;
                end++;
                if (out.length >= MAX_TOKEN_LENGTH - 1) {
                    out = out.slice(0, MAX_TOKEN_LENGTH - 1);
                }
            }

            this.position = end;
            this.current = { type: TOKEN.STRING, value: out, position };
            return;
        }

        if (ch === "/") {
            let treatAsPath = true;
            const next = input[position + 1];
            if (next == null || next === " " || next === "\t" || next === "\n" || next === "\r" || next === "/") {
                treatAsPath = false;
            }

            if (treatAsPath) {
                let hasValidStart = false;
                if (position === 0) {
                    hasValidStart = true;
                } else {
                    let prev = position - 1;
                    while (prev >= 0) {
                        const prevCh = input[prev];
                        if (prevCh === " " || prevCh === "\t" || prevCh === "\r") {
                            prev--;
                            continue;
                        }

                        hasValidStart = prevCh === "\n" || prevCh === ";" || prevCh === "{" || prevCh === "}";
                        break;
                    }

                    if (prev < 0) {
                        hasValidStart = true;
                    }
                }

                if (!hasValidStart) {
                    treatAsPath = false;
                }
            }

            if (treatAsPath) {
                let end = position + 1;
                while (end < input.length) {
                    const current = input[end];
                    if (current === " " || current === "\t" || current === "\n" || current === "\r" || current === ";") {
                        break;
                    }
                    end++;
                }

                const raw = truncateToken(input.slice(position, end));
                this.position = end;
                this.current = { type: TOKEN.PATH, value: raw, position };
                return;
            }
        }

        if (ch === "(" || ch === ")" || ch === "[" || ch === "]" || ch === ";" || ch === "," || ch === "{" || ch === "}") {
            this.position++;
            this.current = { type: ch, value: ch, position };
            return;
        }

        if (ch === "<" || ch === ">" || ch === "!") {
            this.position++;
            let value = ch;
            let type = TOKEN.COMPARISON;
            if ((ch === "<" && input[this.position] === "<") || (ch === ">" && input[this.position] === ">")) {
                value += input[this.position++];
                type = TOKEN.OPERATOR;
            } else if ((ch === "<" && input[this.position] === "=") ||
                       (ch === ">" && input[this.position] === "=") ||
                       (ch === "!" && input[this.position] === "=")) {
                value += input[this.position++];
                type = TOKEN.COMPARISON;
            } else if (ch === "!") {
                type = TOKEN.OPERATOR;
            }

            this.current = { type, value, position };
            return;
        }

        if (ch === "=") {
            this.position++;
            let value = "=";
            let type = TOKEN.OPERATOR;
            if (input[this.position] === "=") {
                value += "=";
                this.position++;
                type = TOKEN.COMPARISON;
            }
            this.current = { type, value, position };
            return;
        }

        this.position++;
        let value = ch;
        if ((ch === "&" && input[this.position] === "&") || (ch === "|" && input[this.position] === "|")) {
            value += input[this.position++];
        }
        this.current = { type: TOKEN.OPERATOR, value, position };
    }
}

class Parser {
    constructor(input) {
        this.input = input;
        this.tokenizer = new Tokenizer(input);
        this.loopDepth = 0;
        this.stats = {
            astNodeCount: 0,
            blockCapacities: [],
            shellCommandBytes: 0,
        };
    }

    createNode(type, fields = {}) {
        this.stats.astNodeCount++;
        return { type, ...fields };
    }

    parse() {
        const root = this.createNode(AST.BLOCK, { statements: [] });
        root.capacity = ROOT_BLOCK_INITIAL_CAPACITY;

        while (this.tokenizer.current.type !== TOKEN.EOF) {
            root.statements.push(this.parseStatement());
        }

        root.capacity = capacityFromCount(root.statements.length, ROOT_BLOCK_INITIAL_CAPACITY);
        this.stats.blockCapacities.push(root.capacity);
        return root;
    }

    parseStatement() {
        const token = this.tokenizer.current;

        if (token.type === TOKEN.IF) {
            return this.parseIf();
        }
        if (token.type === TOKEN.FOR) {
            return this.parseFor();
        }
        if (token.type === TOKEN.RETURN) {
            return this.parseReturn();
        }
        if (token.type === TOKEN.CONTINUE) {
            return this.parseContinue();
        }
        if (token.type === TOKEN.LBRACE) {
            return this.parseBlock();
        }
        if (token.type === TOKEN.PATH || token.type === TOKEN.STRING) {
            return this.parseShellCommand();
        }
        if (token.type === TOKEN.IDENTIFIER) {
            const savedPosition = this.tokenizer.position;
            const savedToken = this.tokenizer.cloneToken();
            let isAssignment = false;

            this.tokenizer.nextToken();
            if (this.tokenizer.current.type === TOKEN.LPAREN) {
                this.tokenizer.restore(savedPosition, savedToken);
                const expression = this.parseComparison();
                return this.finishExpressionStatement(expression);
            }

            for (;;) {
                if (this.tokenizer.current.type === TOKEN.OPERATOR && this.tokenizer.current.value === "=") {
                    isAssignment = true;
                    break;
                }

                if (this.tokenizer.current.type === TOKEN.LBRACKET) {
                    this.tokenizer.nextToken();
                    while (this.tokenizer.current.type !== TOKEN.RBRACKET &&
                           this.tokenizer.current.type !== TOKEN.EOF) {
                        this.tokenizer.nextToken();
                    }

                    if (this.tokenizer.current.type === TOKEN.RBRACKET) {
                        this.tokenizer.nextToken();
                        continue;
                    }
                    break;
                }

                if (this.tokenizer.current.type === TOKEN.OPERATOR && this.tokenizer.current.value === ".") {
                    this.tokenizer.nextToken();
                    if (this.tokenizer.current.type !== TOKEN.IDENTIFIER) {
                        break;
                    }
                    this.tokenizer.nextToken();
                    continue;
                }

                break;
            }

            this.tokenizer.restore(savedPosition, savedToken);
            if (isAssignment) {
                return this.parseAssignment();
            }

            if (this.shouldParseShellCommand()) {
                return this.parseShellCommand();
            }

            const expression = this.parseComparison();
            return this.finishExpressionStatement(expression);
        }

        throw new Error(`Syntax error near token ${token.type}`);
    }

    finishExpressionStatement(expression) {
        if (this.tokenizer.current.type === TOKEN.SEMICOLON) {
            this.tokenizer.nextToken();
        }
        return expression;
    }

    parseReturn() {
        this.expect(TOKEN.RETURN);
        const expression = this.parseComparison();
        this.expectOptionalSemicolonOrBlockEnd();
        return this.createNode(AST.RETURN, { expression });
    }

    parseContinue() {
        if (this.loopDepth === 0) {
            throw new Error("continue outside loop");
        }
        this.expect(TOKEN.CONTINUE);
        this.expectOptionalSemicolonOrBlockEnd();
        return this.createNode(AST.CONTINUE);
    }

    parseIf() {
        this.expect(TOKEN.IF);
        this.expect(TOKEN.LPAREN);
        const condition = this.parseComparison();
        this.expect(TOKEN.RPAREN);
        const thenBranch = this.parseStatement();
        let elseBranch = null;
        if (this.tokenizer.current.type === TOKEN.ELSE) {
            this.tokenizer.nextToken();
            elseBranch = this.parseStatement();
        }
        if (this.tokenizer.current.type === TOKEN.SEMICOLON) {
            this.tokenizer.nextToken();
        }
        return this.createNode(AST.IF, { condition, thenBranch, elseBranch });
    }

    parseFor() {
        this.expect(TOKEN.FOR);
        this.expect(TOKEN.LPAREN);
        const init = this.parseAssignment(true);
        this.expect(TOKEN.SEMICOLON);
        const condition = this.parseComparison();
        this.expect(TOKEN.SEMICOLON);
        const increment = this.parseAssignment(true);
        this.expect(TOKEN.RPAREN);
        this.loopDepth++;
        const body = this.parseStatement();
        this.loopDepth--;
        if (this.tokenizer.current.type === TOKEN.SEMICOLON) {
            this.tokenizer.nextToken();
        }
        return this.createNode(AST.FOR, { init, condition, increment, body });
    }

    parseBlock() {
        this.expect(TOKEN.LBRACE);
        const statements = [];
        while (this.tokenizer.current.type !== TOKEN.RBRACE && this.tokenizer.current.type !== TOKEN.EOF) {
            statements.push(this.parseStatement());
        }
        this.expect(TOKEN.RBRACE);
        const block = this.createNode(AST.BLOCK, { statements });
        block.capacity = capacityFromCount(statements.length, ROOT_BLOCK_INITIAL_CAPACITY);
        this.stats.blockCapacities.push(block.capacity);
        if (this.tokenizer.current.type === TOKEN.SEMICOLON) {
            this.tokenizer.nextToken();
        }
        return block;
    }

    parseAssignment(skipTerminator = false) {
        const identifier = this.expectValue(TOKEN.IDENTIFIER);
        let isArrayAccess = false;
        let arrayIndexExpr = null;
        let isPropertyAccess = false;
        let propertyBaseExpression = null;
        let propertyName = null;
        let targetBaseExpression = null;

        if (this.tokenizer.current.type === TOKEN.LBRACKET) {
            isArrayAccess = true;
            this.tokenizer.nextToken();
            arrayIndexExpr = this.parseComparison();
            this.expect(TOKEN.RBRACKET);
        } else if (this.tokenizer.current.type === TOKEN.OPERATOR && this.tokenizer.current.value === ".") {
            targetBaseExpression = this.createNode(AST.EXPRESSION, {
                tokenType: TOKEN.IDENTIFIER,
                value: identifier,
                isVariable: true,
            });

            while (this.tokenizer.current.type === TOKEN.OPERATOR && this.tokenizer.current.value === ".") {
                this.tokenizer.nextToken();
                const prop = this.expectValue(TOKEN.IDENTIFIER);
                if (this.tokenizer.current.type === TOKEN.OPERATOR && this.tokenizer.current.value === ".") {
                    targetBaseExpression = this.createNode(AST.EXPRESSION, {
                        tokenType: TOKEN.IDENTIFIER,
                        isVariable: false,
                        isPropertyAccess: true,
                        baseExpression: targetBaseExpression,
                        propertyName: prop,
                    });
                    continue;
                }

                isPropertyAccess = true;
                propertyBaseExpression = targetBaseExpression;
                propertyName = prop;
                targetBaseExpression = null;
                break;
            }
        }

        this.expectOperator("=");
        const expression = this.parseComparison();
        if (!skipTerminator) {
            this.expectOptionalSemicolonOrBlockEnd();
        }
        return this.createNode(AST.ASSIGNMENT, {
            varName: identifier,
            expression,
            isArrayAccess,
            arrayIndexExpr,
            isPropertyAccess,
            propertyBaseExpression,
            propertyName,
        });
    }

    parseComparison() {
        return this.parseLogicalOr();
    }

    parseLogicalOr() {
        let left = this.parseLogicalAnd();
        while (this.tokenizer.current.type === TOKEN.OPERATOR && this.tokenizer.current.value === "||") {
            const op = this.tokenizer.current.value;
            this.tokenizer.nextToken();
            const right = this.parseLogicalAnd();
            left = this.createBinaryNode(TOKEN.OPERATOR, op, left, right);
        }
        return left;
    }

    parseLogicalAnd() {
        let left = this.parseRelational();
        while (this.tokenizer.current.type === TOKEN.OPERATOR && this.tokenizer.current.value === "&&") {
            const op = this.tokenizer.current.value;
            this.tokenizer.nextToken();
            const right = this.parseRelational();
            left = this.createBinaryNode(TOKEN.OPERATOR, op, left, right);
        }
        return left;
    }

    parseRelational() {
        let left = this.parseBitwiseOr();
        while (this.tokenizer.current.type === TOKEN.COMPARISON) {
            const op = this.tokenizer.current.value;
            this.tokenizer.nextToken();
            const right = this.parseBitwiseOr();
            left = this.createBinaryNode(TOKEN.COMPARISON, op, left, right);
        }
        return left;
    }

    parseBitwiseOr() {
        let left = this.parseBitwiseXor();
        while (this.tokenizer.current.type === TOKEN.OPERATOR && this.tokenizer.current.value === "|") {
            const op = this.tokenizer.current.value;
            this.tokenizer.nextToken();
            const right = this.parseBitwiseXor();
            left = this.createBinaryNode(TOKEN.OPERATOR, op, left, right);
        }
        return left;
    }

    parseBitwiseXor() {
        let left = this.parseBitwiseAnd();
        while (this.tokenizer.current.type === TOKEN.OPERATOR && this.tokenizer.current.value === "^") {
            const op = this.tokenizer.current.value;
            this.tokenizer.nextToken();
            const right = this.parseBitwiseAnd();
            left = this.createBinaryNode(TOKEN.OPERATOR, op, left, right);
        }
        return left;
    }

    parseBitwiseAnd() {
        let left = this.parseShift();
        while (this.tokenizer.current.type === TOKEN.OPERATOR && this.tokenizer.current.value === "&") {
            const op = this.tokenizer.current.value;
            this.tokenizer.nextToken();
            const right = this.parseShift();
            left = this.createBinaryNode(TOKEN.OPERATOR, op, left, right);
        }
        return left;
    }

    parseShift() {
        let left = this.parseExpression();
        while (this.tokenizer.current.type === TOKEN.OPERATOR &&
               (this.tokenizer.current.value === "<<" || this.tokenizer.current.value === ">>")) {
            const op = this.tokenizer.current.value;
            this.tokenizer.nextToken();
            const right = this.parseExpression();
            left = this.createBinaryNode(TOKEN.OPERATOR, op, left, right);
        }
        return left;
    }

    parseExpression() {
        let left = this.parseTerm();
        while (this.tokenizer.current.type === TOKEN.OPERATOR &&
               (this.tokenizer.current.value === "+" || this.tokenizer.current.value === "-")) {
            const op = this.tokenizer.current.value;
            this.tokenizer.nextToken();
            const right = this.parseTerm();
            left = this.createBinaryNode(TOKEN.OPERATOR, op, left, right);
        }
        return left;
    }

    parseTerm() {
        let left = this.parseFactor();
        while (this.tokenizer.current.type === TOKEN.OPERATOR &&
               (this.tokenizer.current.value === "*" || this.tokenizer.current.value === "/")) {
            const op = this.tokenizer.current.value;
            this.tokenizer.nextToken();
            const right = this.parseFactor();
            left = this.createBinaryNode(TOKEN.OPERATOR, op, left, right);
        }
        return left;
    }

    parseFactor() {
        if (this.tokenizer.current.type === TOKEN.OPERATOR &&
            ["+", "-", "!", "~"].includes(this.tokenizer.current.value)) {
            const unary = this.tokenizer.current.value;
            this.tokenizer.nextToken();
            const operand = this.parseFactor();
            if (unary === "!" || unary === "~") {
                return this.createNode(AST.EXPRESSION, {
                    tokenType: TOKEN.OPERATOR,
                    value: unary,
                    right: operand,
                });
            }
            const zero = this.createNode(AST.EXPRESSION, {
                tokenType: TOKEN.NUMBER,
                value: "0",
                isIntegerLiteral: true,
                integerValue: 0,
                floatValue: 0,
            });
            return this.createBinaryNode(TOKEN.OPERATOR, unary, zero, operand);
        }

        if (this.tokenizer.current.type === TOKEN.NUMBER) {
            const token = this.tokenizer.current;
            this.tokenizer.nextToken();
            return this.createNode(AST.EXPRESSION, {
                tokenType: TOKEN.NUMBER,
                value: token.value,
                isIntegerLiteral: token.isInteger,
                integerValue: token.integerValue,
                floatValue: token.floatValue,
            });
        }

        if (this.tokenizer.current.type === TOKEN.IDENTIFIER) {
            let node = this.createNode(AST.EXPRESSION, {
                tokenType: TOKEN.IDENTIFIER,
                value: this.tokenizer.current.value,
                isVariable: true,
            });
            this.tokenizer.nextToken();

            if (this.tokenizer.current.type === TOKEN.LPAREN) {
                node.isFunctionCall = true;
                this.tokenizer.nextToken();
                node.arguments = [];
                if (this.tokenizer.current.type !== TOKEN.RPAREN) {
                    for (;;) {
                        node.arguments.push(this.parseComparison());
                        if (this.tokenizer.current.type === TOKEN.COMMA) {
                            this.tokenizer.nextToken();
                            continue;
                        }
                        break;
                    }
                }
                this.expect(TOKEN.RPAREN);
            }

            for (;;) {
                if (this.tokenizer.current.type === TOKEN.LBRACKET) {
                    this.tokenizer.nextToken();
                    const index = this.parseComparison();
                    this.expect(TOKEN.RBRACKET);
                    node = this.createNode(AST.EXPRESSION, {
                        tokenType: TOKEN.IDENTIFIER,
                        isArrayAccess: true,
                        baseExpression: node,
                        arrayIndexExpr: index,
                    });
                    continue;
                }

                if (this.tokenizer.current.type === TOKEN.OPERATOR && this.tokenizer.current.value === ".") {
                    this.tokenizer.nextToken();
                    const prop = this.expectValue(TOKEN.IDENTIFIER);
                    node = this.createNode(AST.EXPRESSION, {
                        tokenType: TOKEN.IDENTIFIER,
                        isPropertyAccess: true,
                        baseExpression: node,
                        propertyName: prop,
                    });
                    continue;
                }

                break;
            }

            return node;
        }

        if (this.tokenizer.current.type === TOKEN.STRING) {
            const token = this.tokenizer.current;
            this.tokenizer.nextToken();
            return this.createNode(AST.EXPRESSION, {
                tokenType: TOKEN.STRING,
                value: token.value,
            });
        }

        if (this.tokenizer.current.type === TOKEN.LBRACE) {
            this.tokenizer.nextToken();
            this.expect(TOKEN.RBRACE);
            return this.createNode(AST.EXPRESSION, {
                tokenType: TOKEN.LBRACE,
                value: "{}",
            });
        }

        if (this.tokenizer.current.type === TOKEN.LPAREN) {
            this.tokenizer.nextToken();
            const expr = this.parseComparison();
            this.expect(TOKEN.RPAREN);
            return expr;
        }

        throw new Error(`Unexpected token ${this.tokenizer.current.type}`);
    }

    createBinaryNode(tokenType, value, left, right) {
        return this.createNode(AST.EXPRESSION, {
            tokenType,
            value,
            left,
            right,
        });
    }

    shouldParseShellCommand() {
        if (this.tokenizer.current.type === TOKEN.STRING || this.tokenizer.current.type === TOKEN.PATH) {
            return true;
        }

        if (this.tokenizer.current.type !== TOKEN.IDENTIFIER) {
            return false;
        }

        let pos = this.tokenizer.position;
        while (pos < this.input.length && (this.input[pos] === " " || this.input[pos] === "\t")) {
            pos++;
        }
        return this.input[pos] !== "(";
    }

    parseShellCommand() {
        const initialToken = this.tokenizer.current;
        const start = initialToken.position;
        let scan = start;
        let inQuotes = false;
        let quoteChar = "";

        while (scan < this.input.length) {
            const ch = this.input[scan];
            if (!inQuotes && (ch === ";" || ch === "\n" || ch === "\r")) {
                break;
            }

            if (ch === "\"" || ch === "'") {
                if (inQuotes && ch === quoteChar) {
                    inQuotes = false;
                    quoteChar = "";
                } else if (!inQuotes) {
                    inQuotes = true;
                    quoteChar = ch;
                }
            }

            scan++;
        }

        let end = scan;
        while (end > start && (this.input[end - 1] === " " || this.input[end - 1] === "\t")) {
            end--;
        }

        if (end <= start) {
            throw new Error("Invalid shell command");
        }

        const commandLine = this.input.slice(start, end);
        this.stats.shellCommandBytes += Buffer.byteLength(commandLine, "utf8") + 1;
        this.tokenizer.position = scan;
        this.tokenizer.nextToken();

        if (this.tokenizer.current.type === TOKEN.SEMICOLON) {
            this.tokenizer.nextToken();
        }

        return this.createNode(AST.EXPRESSION, {
            tokenType: initialToken.type === TOKEN.PATH ? TOKEN.PATH : TOKEN.IDENTIFIER,
            value: truncateToken(commandLine.trim().replace(/^["']/, "").split(/["'\s]/, 1)[0]),
            isFunctionCall: true,
            isShellCommand: true,
            commandLine,
            arguments: [],
        });
    }

    expect(type) {
        if (this.tokenizer.current.type !== type) {
            throw new Error(`Expected ${type}, got ${this.tokenizer.current.type}`);
        }
        this.tokenizer.nextToken();
    }

    expectValue(type) {
        if (this.tokenizer.current.type !== type) {
            throw new Error(`Expected ${type}, got ${this.tokenizer.current.type}`);
        }
        const value = this.tokenizer.current.value;
        this.tokenizer.nextToken();
        return value;
    }

    expectOperator(value) {
        if (this.tokenizer.current.type !== TOKEN.OPERATOR || this.tokenizer.current.value !== value) {
            throw new Error(`Expected operator ${value}`);
        }
        this.tokenizer.nextToken();
    }

    expectOptionalSemicolonOrBlockEnd() {
        if (this.tokenizer.current.type === TOKEN.SEMICOLON) {
            this.tokenizer.nextToken();
            return;
        }

        if (this.tokenizer.current.type === TOKEN.EOF || this.tokenizer.current.type === TOKEN.RBRACE) {
            return;
        }

        throw new Error("Expected semicolon");
    }
}

class ObjectEntity {
    constructor(simulator) {
        this.simulator = simulator;
        this.refCount = 1;
        this.properties = new Map();
        this.propertyCount = 0;
        this.propertyCapacity = OBJECT_INITIAL_CAPACITY;
        this.objectAllocId = simulator.memory.allocate(simulator.model.scriptObjectSize, "object-struct");
        this.propertiesAllocId = simulator.memory.allocate(
            simulator.model.scriptObjectPropertySize * this.propertyCapacity,
            "object-properties"
        );
    }

    retain() {
        this.refCount++;
    }

    release() {
        this.refCount--;
        if (this.refCount > 0) {
            return;
        }

        for (const stored of this.properties.values()) {
            this.simulator.releaseStoredValue(stored);
        }
        this.properties.clear();
        this.simulator.memory.free(this.propertiesAllocId);
        this.simulator.memory.free(this.objectAllocId);
    }

    setProperty(name, runtimeValue) {
        let slot = this.properties.get(name);
        if (!slot) {
            if (this.propertyCount >= this.propertyCapacity) {
                const newCapacity = this.propertyCapacity === 0 ? OBJECT_INITIAL_CAPACITY : this.propertyCapacity * 2;
                const newAlloc = this.simulator.memory.allocate(
                    this.simulator.model.scriptObjectPropertySize * newCapacity,
                    "object-properties"
                );
                this.simulator.memory.free(this.propertiesAllocId);
                this.propertiesAllocId = newAlloc;
                this.propertyCapacity = newCapacity;
            }

            slot = null;
            this.propertyCount++;
        } else {
            this.simulator.releaseStoredValue(slot);
        }

        const stored = this.simulator.storeValue(runtimeValue, "object-property");
        this.properties.set(name, stored);
    }

    getProperty(name) {
        return this.properties.get(name) || null;
    }
}

class ArrayEntity {
    constructor(simulator) {
        this.simulator = simulator;
        this.capacity = ARRAY_INITIAL_CAPACITY;
        this.size = 0;
        this.entries = new Map();
        this.arrayAllocId = simulator.memory.allocate(simulator.model.scriptArraySize, "array-struct");
        this.elementsAllocId = simulator.memory.allocate(
            this.capacity * simulator.model.pointerSize,
            "array-elements"
        );
        this.typesAllocId = simulator.memory.allocate(
            this.capacity * simulator.model.enumSize,
            "array-types"
        );
    }

    release() {
        for (const stored of this.entries.values()) {
            this.simulator.releaseStoredValue(stored);
        }
        this.entries.clear();
        this.simulator.memory.free(this.elementsAllocId);
        this.simulator.memory.free(this.typesAllocId);
        this.simulator.memory.free(this.arrayAllocId);
    }

    set(index, runtimeValue) {
        if (index >= this.capacity) {
            let newCapacity = index + 1;
            if (newCapacity < this.capacity * 2) {
                newCapacity = this.capacity * 2;
            }

            const newElements = this.simulator.memory.allocate(
                newCapacity * this.simulator.model.pointerSize,
                "array-elements"
            );
            const newTypes = this.simulator.memory.allocate(
                newCapacity * this.simulator.model.enumSize,
                "array-types"
            );
            this.simulator.memory.free(this.elementsAllocId);
            this.simulator.memory.free(this.typesAllocId);
            this.elementsAllocId = newElements;
            this.typesAllocId = newTypes;
            this.capacity = newCapacity;
        }

        if (this.entries.has(index)) {
            this.simulator.releaseStoredValue(this.entries.get(index));
        }

        this.entries.set(index, this.simulator.storeValue(runtimeValue, "array-element"));
        if (index >= this.size) {
            this.size = index + 1;
        }
    }

    get(index) {
        return this.entries.get(index) || null;
    }
}

class Simulator {
    constructor(model, ast, parseStats) {
        this.model = model;
        this.ast = ast;
        this.parseStats = parseStats;
        this.memory = new MemoryTracker();
        this.variables = new Map();
        this.returnSlot = null;
        this.returnTriggered = false;
        this.continueTriggered = false;
        this.notes = [];
        this.dynamicResolved = true;
    }

    run() {
        this.allocateFixedInterpreter();
        this.allocateAst();
        this.executeBlock(this.ast);
        this.releaseAst();

        return {
            fixedInterpreterBytes: this.model.fixedInterpreterBytes,
            astBytes: this.computeAstBytes(),
            peakBytes: this.memory.peakBytes,
            retainedBytesAfterExecution: this.memory.currentBytes,
            dynamicResolved: this.dynamicResolved,
            notes: this.notes,
            liveVariableCount: this.variables.size,
        };
    }

    allocateFixedInterpreter() {
        this.fixedContextAllocId = this.memory.allocate(this.model.scriptContextSize, "context");
        this.fixedScopeAllocId = this.memory.allocate(this.model.scriptScopeSize, "scope");
        this.scopeListIds = [];
        this.hostListIds = [];
        for (let index = 0; index < SCRIPT_VAR_HASH_SIZE; index++) {
            this.scopeListIds.push(this.memory.allocate(this.model.listSize, "scope-list"));
            this.hostListIds.push(this.memory.allocate(this.model.listSize, "host-list"));
        }
    }

    allocateAst() {
        this.astNodeAllocIds = [];
        for (let index = 0; index < this.parseStats.astNodeCount; index++) {
            this.astNodeAllocIds.push(this.memory.allocate(this.model.astNodeSize, "ast-node"));
        }

        this.blockAllocIds = [];
        for (const capacity of this.parseStats.blockCapacities) {
            this.blockAllocIds.push(this.memory.allocate(capacity * this.model.pointerSize, "ast-block-vector"));
        }

        this.shellCommandAllocIds = [];
        let remaining = this.parseStats.shellCommandBytes;
        while (remaining > 0) {
            this.shellCommandAllocIds.push(this.memory.allocate(remaining, "ast-shell-command"));
            remaining = 0;
        }
    }

    releaseAst() {
        for (const id of this.astNodeAllocIds) {
            this.memory.free(id);
        }
        for (const id of this.blockAllocIds) {
            this.memory.free(id);
        }
        for (const id of this.shellCommandAllocIds) {
            this.memory.free(id);
        }
    }

    computeAstBytes() {
        let total = this.parseStats.astNodeCount * this.model.astNodeSize;
        for (const capacity of this.parseStats.blockCapacities) {
            total += capacity * this.model.pointerSize;
        }
        total += this.parseStats.shellCommandBytes;
        return total;
    }

    executeBlock(block) {
        for (const statement of block.statements) {
            this.executeStatement(statement);
            if (this.returnTriggered || this.continueTriggered) {
                return;
            }
        }
    }

    executeStatement(node) {
        switch (node.type) {
            case AST.ASSIGNMENT:
                this.executeAssignment(node);
                return;
            case AST.BLOCK:
                this.executeBlock(node);
                return;
            case AST.IF: {
                const condition = this.evaluateExpression(node.condition);
                const numeric = this.valueToFloat(condition);
                this.releaseValue(condition);
                if (numeric == null) {
                    this.markDynamicGap("if condition depends on an unresolved value");
                    return;
                }
                if (numeric !== 0) {
                    this.executeStatement(node.thenBranch);
                } else if (node.elseBranch) {
                    this.executeStatement(node.elseBranch);
                }
                return;
            }
            case AST.FOR: {
                this.executeStatement(node.init);
                let loopCount = 0;
                while (loopCount < FOR_MAX_ITERATIONS) {
                    const condition = this.evaluateExpression(node.condition);
                    const numeric = this.valueToFloat(condition);
                    this.releaseValue(condition);
                    if (numeric == null) {
                        this.markDynamicGap("for condition depends on an unresolved value");
                        return;
                    }
                    if (numeric === 0) {
                        break;
                    }

                    this.executeStatement(node.body);
                    if (this.returnTriggered) {
                        return;
                    }
                    if (this.continueTriggered) {
                        this.continueTriggered = false;
                    }

                    this.executeStatement(node.increment);
                    if (this.returnTriggered) {
                        return;
                    }

                    loopCount++;
                }
                if (loopCount >= FOR_MAX_ITERATIONS) {
                    this.notes.push("Loop execution hit the E0 hard cap of 1000 iterations.");
                }
                return;
            }
            case AST.RETURN: {
                const value = this.evaluateExpression(node.expression);
                this.storeReturnValue(value);
                this.releaseValue(value);
                this.returnTriggered = true;
                return;
            }
            case AST.CONTINUE:
                this.continueTriggered = true;
                return;
            case AST.EXPRESSION: {
                const value = this.evaluateExpression(node);
                this.releaseValue(value);
                return;
            }
            default:
                throw new Error(`Unsupported AST node ${node.type}`);
        }
    }

    executeAssignment(node) {
        const evaluated = this.evaluateExpression(node.expression);
        if (evaluated.type === VALUE.HOST) {
            this.markDynamicGap("host handles are not assignable in E0 memory estimation");
            this.releaseValue(evaluated);
            return;
        }

        let assignmentValue = evaluated;
        if (evaluated.type === VALUE.FLOAT && isIntegerNumber(evaluated.number)) {
            assignmentValue = { type: VALUE.INTEGER, number: Math.trunc(evaluated.number), owned: false };
        }

        if (node.isArrayAccess) {
            const indexValue = this.evaluateExpression(node.arrayIndexExpr);
            const index = this.valueToInteger(indexValue);
            this.releaseValue(indexValue);
            if (index == null || index < 0) {
                this.markDynamicGap("array index could not be resolved");
                this.releaseValue(evaluated);
                return;
            }

            let variable = this.variables.get(node.varName);
            if (!variable) {
                const array = new ArrayEntity(this);
                const arrayStored = { type: VALUE.ARRAY, entity: array, persistent: true };
                const variableAllocId = this.memory.allocate(this.model.scriptVariableSize, "variable");
                variable = {
                    name: node.varName,
                    allocId: variableAllocId,
                    stored: arrayStored,
                };
                this.variables.set(node.varName, variable);
            }

            if (variable.stored.type !== VALUE.ARRAY) {
                this.markDynamicGap(`variable ${node.varName} is not an array`);
                this.releaseValue(evaluated);
                return;
            }

            variable.stored.entity.set(index, assignmentValue);
            this.releaseValue(evaluated);
            return;
        }

        if (node.isPropertyAccess) {
            const baseValue = this.evaluateExpression(node.propertyBaseExpression);
            if (baseValue.type !== VALUE.OBJECT || !baseValue.entity) {
                this.markDynamicGap(`property base for ${node.propertyName} is not a native object`);
                this.releaseValue(baseValue);
                this.releaseValue(evaluated);
                return;
            }

            baseValue.entity.setProperty(node.propertyName, assignmentValue);
            this.releaseValue(baseValue);
            this.releaseValue(evaluated);
            return;
        }

        let variable = this.variables.get(node.varName);
        if (!variable) {
            variable = {
                name: node.varName,
                allocId: this.memory.allocate(this.model.scriptVariableSize, "variable"),
                stored: null,
            };
            this.variables.set(node.varName, variable);
        } else {
            this.releaseStoredValue(variable.stored);
        }

        variable.stored = this.storeValue(assignmentValue, "variable");
        this.releaseValue(evaluated);
    }

    storeReturnValue(value) {
        if (this.returnSlot) {
            this.releaseStoredValue(this.returnSlot);
            this.returnSlot = null;
        }

        if (value.type === VALUE.HOST || value.type === VALUE.ARRAY) {
            this.markDynamicGap("return values of type host-handle or array are not stored by E0");
            return;
        }

        this.returnSlot = this.storeValue(value, "return");
    }

    storeValue(value, ownerTag) {
        switch (value.type) {
            case VALUE.STRING: {
                const text = value.text || "";
                const allocId = this.memory.allocate(Buffer.byteLength(text, "utf8") + 1, `${ownerTag}-string`);
                return {
                    type: VALUE.STRING,
                    text,
                    allocId,
                    persistent: true,
                };
            }
            case VALUE.INTEGER:
                return { type: VALUE.INTEGER, number: value.number, persistent: true };
            case VALUE.FLOAT:
                return { type: VALUE.FLOAT, number: value.number, persistent: true };
            case VALUE.OBJECT:
                value.entity.retain();
                return { type: VALUE.OBJECT, entity: value.entity, persistent: true };
            case VALUE.ARRAY:
                return { type: VALUE.ARRAY, entity: value.entity, persistent: true };
            case VALUE.UNKNOWN:
                return { type: VALUE.UNKNOWN, persistent: true };
            default:
                throw new Error(`Unsupported stored type ${value.type}`);
        }
    }

    releaseStoredValue(stored) {
        if (!stored) {
            return;
        }

        if (stored.type === VALUE.STRING) {
            this.memory.free(stored.allocId);
        } else if (stored.type === VALUE.OBJECT) {
            stored.entity.release();
        }
    }

    releaseValue(value) {
        if (!value || !value.owned) {
            return;
        }

        if (value.type === VALUE.STRING) {
            this.memory.free(value.allocId);
        } else if (value.type === VALUE.OBJECT) {
            value.entity.release();
        }
    }

    evaluateExpression(node) {
        if (!node || node.type !== AST.EXPRESSION) {
            return { type: VALUE.UNKNOWN };
        }

        if (node.isPropertyAccess) {
            const base = this.evaluateExpression(node.baseExpression);
            if (base.type === VALUE.OBJECT && base.entity) {
                const property = base.entity.getProperty(node.propertyName);
                this.releaseValue(base);
                if (!property) {
                    this.markDynamicGap(`missing object property ${node.propertyName}`);
                    return { type: VALUE.UNKNOWN };
                }
                return this.borrowStoredValue(property);
            }
            this.releaseValue(base);
            this.markDynamicGap(`property access on unresolved host or non-object value (${node.propertyName})`);
            return { type: VALUE.UNKNOWN };
        }

        if (node.isArrayAccess && node.baseExpression) {
            const base = this.evaluateExpression(node.baseExpression);
            const indexValue = this.evaluateExpression(node.arrayIndexExpr);
            const index = this.valueToInteger(indexValue);
            this.releaseValue(indexValue);
            if (base.type !== VALUE.ARRAY || !base.entity || index == null || index < 0) {
                this.releaseValue(base);
                this.markDynamicGap("array access could not be resolved");
                return { type: VALUE.UNKNOWN };
            }
            const stored = base.entity.get(index);
            this.releaseValue(base);
            if (!stored) {
                this.markDynamicGap(`array element ${index} is unset`);
                return { type: VALUE.UNKNOWN };
            }
            const tempId = this.memory.allocate(this.model.scriptVariableSize, "array-read-temp");
            const borrowed = this.borrowStoredValue(stored);
            this.memory.free(tempId);
            return borrowed;
        }

        switch (node.tokenType) {
            case TOKEN.NUMBER:
                if (node.isIntegerLiteral) {
                    return { type: VALUE.INTEGER, number: node.integerValue, owned: false };
                }
                return { type: VALUE.FLOAT, number: node.floatValue, owned: false };
            case TOKEN.LBRACE:
                return { type: VALUE.OBJECT, entity: new ObjectEntity(this), owned: true };
            case TOKEN.STRING: {
                const text = node.value || "";
                const allocId = this.memory.allocate(Buffer.byteLength(text, "utf8") + 1, "temp-string");
                return { type: VALUE.STRING, text, allocId, owned: true };
            }
            case TOKEN.PATH:
            case TOKEN.IDENTIFIER:
                if (node.isFunctionCall) {
                    return this.evaluateFunctionCall(node);
                }
                if (node.isArrayAccess && !node.baseExpression) {
                    const variable = this.variables.get(node.value);
                    if (!variable || variable.stored.type !== VALUE.ARRAY) {
                        this.markDynamicGap(`array variable ${node.value} is unresolved`);
                        return { type: VALUE.UNKNOWN };
                    }

                    const indexValue = this.evaluateExpression(node.arrayIndexExpr);
                    const index = this.valueToInteger(indexValue);
                    this.releaseValue(indexValue);
                    if (index == null || index < 0) {
                        this.markDynamicGap(`array index for ${node.value} is unresolved`);
                        return { type: VALUE.UNKNOWN };
                    }

                    const stored = variable.stored.entity.get(index);
                    if (!stored) {
                        this.markDynamicGap(`array element ${node.value}[${index}] is unset`);
                        return { type: VALUE.UNKNOWN };
                    }

                    const tempId = this.memory.allocate(this.model.scriptVariableSize, "array-read-temp");
                    const borrowed = this.borrowStoredValue(stored);
                    this.memory.free(tempId);
                    return borrowed;
                }

                if (!this.variables.has(node.value)) {
                    this.markDynamicGap(`identifier ${node.value} depends on host state or prior shell context`);
                    return { type: VALUE.UNKNOWN };
                }
                return this.borrowStoredValue(this.variables.get(node.value).stored);
            case TOKEN.OPERATOR:
            case TOKEN.COMPARISON:
                return this.evaluateOperator(node);
            default:
                this.markDynamicGap(`unsupported expression token ${node.tokenType}`);
                return { type: VALUE.UNKNOWN };
        }
    }

    evaluateFunctionCall(node) {
        if (node.isShellCommand) {
            return { type: VALUE.INTEGER, number: 0, owned: false };
        }

        const argumentCount = node.arguments ? node.arguments.length : 0;
        const argsAllocId = argumentCount > 0 ? this.memory.allocate(argumentCount * this.model.pointerSize, "call-argv") : null;
        const ownedAllocId = argumentCount > 0 ? this.memory.allocate(argumentCount * this.model.boolSize, "call-owned") : null;
        const temporaryStringIds = [];

        try {
            for (const argumentNode of node.arguments || []) {
                const value = this.evaluateExpression(argumentNode);
                const stringified = this.valueToString(value);
                this.releaseValue(value);
                if (stringified == null) {
                    this.markDynamicGap(`function argument for ${node.value} could not be stringified`);
                    continue;
                }
                const stableId = this.memory.allocate(Buffer.byteLength(stringified.text, "utf8") + 1, "call-arg-copy");
                temporaryStringIds.push(stableId);
                if (stringified.ownedTempId) {
                    this.memory.free(stringified.ownedTempId);
                }
            }
        } finally {
            for (const id of temporaryStringIds) {
                this.memory.free(id);
            }
            this.memory.free(ownedAllocId);
            this.memory.free(argsAllocId);
        }

        return { type: VALUE.INTEGER, number: 0, owned: false };
    }

    evaluateOperator(node) {
        const op = node.value;

        if (op === "!") {
            const right = this.evaluateExpression(node.right);
            const truth = this.valueToBoolean(right);
            this.releaseValue(right);
            if (truth == null) {
                this.markDynamicGap("logical not operand is unresolved");
                return { type: VALUE.UNKNOWN };
            }
            return { type: VALUE.INTEGER, number: truth ? 0 : 1, owned: false };
        }

        if (op === "~") {
            const right = this.evaluateExpression(node.right);
            const integer = this.valueToInteger(right);
            this.releaseValue(right);
            if (integer == null) {
                this.markDynamicGap("bitwise not operand is unresolved");
                return { type: VALUE.UNKNOWN };
            }
            return { type: VALUE.INTEGER, number: ~integer, owned: false };
        }

        if (op === "&&" || op === "||") {
            const left = this.evaluateExpression(node.left);
            const leftTruth = this.valueToBoolean(left);
            if (leftTruth == null) {
                this.releaseValue(left);
                this.markDynamicGap(`logical operator ${op} left operand is unresolved`);
                return { type: VALUE.UNKNOWN };
            }
            if (op === "&&" && !leftTruth) {
                this.releaseValue(left);
                return { type: VALUE.INTEGER, number: 0, owned: false };
            }
            if (op === "||" && leftTruth) {
                this.releaseValue(left);
                return { type: VALUE.INTEGER, number: 1, owned: false };
            }

            const right = this.evaluateExpression(node.right);
            const rightTruth = this.valueToBoolean(right);
            this.releaseValue(left);
            this.releaseValue(right);
            if (rightTruth == null) {
                this.markDynamicGap(`logical operator ${op} right operand is unresolved`);
                return { type: VALUE.UNKNOWN };
            }
            return { type: VALUE.INTEGER, number: rightTruth ? 1 : 0, owned: false };
        }

        const left = this.evaluateExpression(node.left);
        const right = this.evaluateExpression(node.right);

        if (op === "+" && (left.type === VALUE.STRING || right.type === VALUE.STRING)) {
            const result = this.concatStrings(left, right);
            this.releaseValue(left);
            this.releaseValue(right);
            return result;
        }

        if (op === "-" && (left.type === VALUE.STRING || right.type === VALUE.STRING)) {
            const result = this.removeStringOccurrences(left, right);
            this.releaseValue(left);
            this.releaseValue(right);
            return result;
        }

        if (["&", "|", "^", "<<", ">>"].includes(op)) {
            const leftInteger = this.valueToInteger(left);
            const rightInteger = this.valueToInteger(right);
            this.releaseValue(left);
            this.releaseValue(right);
            if (leftInteger == null || rightInteger == null) {
                this.markDynamicGap(`integer operator ${op} has unresolved operands`);
                return { type: VALUE.UNKNOWN };
            }
            if ((op === "<<" || op === ">>") &&
                (rightInteger < 0 || rightInteger >= (this.model.intSize * 8))) {
                this.markDynamicGap(`shift count ${rightInteger} is invalid`);
                return { type: VALUE.UNKNOWN };
            }
            const table = {
                "&": leftInteger & rightInteger,
                "|": leftInteger | rightInteger,
                "^": leftInteger ^ rightInteger,
                "<<": leftInteger << rightInteger,
                ">>": leftInteger >> rightInteger,
            };
            return { type: VALUE.INTEGER, number: table[op], owned: false };
        }

        if (node.tokenType === TOKEN.COMPARISON) {
            const result = this.evaluateComparison(op, left, right);
            this.releaseValue(left);
            this.releaseValue(right);
            return result;
        }

        const leftFloat = this.valueToFloat(left);
        const rightFloat = this.valueToFloat(right);
        const bothIntegers = left.type === VALUE.INTEGER && right.type === VALUE.INTEGER;
        this.releaseValue(left);
        this.releaseValue(right);
        if (leftFloat == null || rightFloat == null) {
            this.markDynamicGap(`operator ${op} has unresolved numeric operands`);
            return { type: VALUE.UNKNOWN };
        }

        if (bothIntegers) {
            if ((op === "/" && rightFloat === 0) || !["+", "-", "*", "/"].includes(op)) {
                this.markDynamicGap(`invalid integer operator ${op}`);
                return { type: VALUE.UNKNOWN };
            }
            const intResult = {
                "+": Math.trunc(leftFloat + rightFloat),
                "-": Math.trunc(leftFloat - rightFloat),
                "*": Math.trunc(leftFloat * rightFloat),
                "/": Math.trunc(leftFloat / rightFloat),
            }[op];
            return { type: VALUE.INTEGER, number: intResult, owned: false };
        }

        if (op === "/" && rightFloat === 0) {
            this.markDynamicGap("division by zero");
            return { type: VALUE.UNKNOWN };
        }

        const floatResult = {
            "+": leftFloat + rightFloat,
            "-": leftFloat - rightFloat,
            "*": leftFloat * rightFloat,
            "/": leftFloat / rightFloat,
        }[op];
        if (floatResult == null) {
            this.markDynamicGap(`invalid operator ${op}`);
            return { type: VALUE.UNKNOWN };
        }
        return { type: VALUE.FLOAT, number: floatResult, owned: false };
    }

    evaluateComparison(op, left, right) {
        if ((left.type === VALUE.STRING || right.type === VALUE.STRING) && (op === "==" || op === "!=")) {
            if (left.type !== VALUE.STRING || right.type !== VALUE.STRING) {
                this.markDynamicGap(`string comparison ${op} mixes incompatible types`);
                return { type: VALUE.UNKNOWN };
            }
            const equal = (left.text || "") === (right.text || "");
            return { type: VALUE.INTEGER, number: op === "==" ? (equal ? 1 : 0) : (equal ? 0 : 1), owned: false };
        }

        const leftFloat = this.valueToFloat(left);
        const rightFloat = this.valueToFloat(right);
        if (leftFloat == null || rightFloat == null) {
            this.markDynamicGap(`comparison ${op} has unresolved operands`);
            return { type: VALUE.UNKNOWN };
        }

        const result = {
            "<": leftFloat < rightFloat,
            "<=": leftFloat <= rightFloat,
            ">": leftFloat > rightFloat,
            ">=": leftFloat >= rightFloat,
            "==": leftFloat === rightFloat,
            "!=": leftFloat !== rightFloat,
        }[op];

        return { type: VALUE.INTEGER, number: result ? 1 : 0, owned: false };
    }

    concatStrings(left, right) {
        const leftText = this.valueToString(left);
        const rightText = this.valueToString(right);
        if (leftText == null || rightText == null) {
            if (leftText && leftText.ownedTempId) {
                this.memory.free(leftText.ownedTempId);
            }
            if (rightText && rightText.ownedTempId) {
                this.memory.free(rightText.ownedTempId);
            }
            this.markDynamicGap("string concatenation could not stringify one operand");
            return { type: VALUE.UNKNOWN };
        }

        const text = leftText.text + rightText.text;
        const allocId = this.memory.allocate(Buffer.byteLength(text, "utf8") + 1, "temp-concat");
        if (leftText.ownedTempId) {
            this.memory.free(leftText.ownedTempId);
        }
        if (rightText.ownedTempId) {
            this.memory.free(rightText.ownedTempId);
        }
        return { type: VALUE.STRING, text, allocId, owned: true };
    }

    removeStringOccurrences(left, right) {
        if (left.type !== VALUE.STRING || right.type !== VALUE.STRING) {
            this.markDynamicGap("string subtraction requires two strings");
            return { type: VALUE.UNKNOWN };
        }

        const source = left.text || "";
        const pattern = right.text || "";
        const text = pattern.length === 0 ? source : source.split(pattern).join("");
        const allocId = this.memory.allocate(Buffer.byteLength(source, "utf8") + 1, "temp-string-remove");
        return { type: VALUE.STRING, text, allocId, owned: true };
    }

    borrowStoredValue(stored) {
        switch (stored.type) {
            case VALUE.STRING:
                return { type: VALUE.STRING, text: stored.text, owned: false };
            case VALUE.INTEGER:
                return { type: VALUE.INTEGER, number: stored.number, owned: false };
            case VALUE.FLOAT:
                return { type: VALUE.FLOAT, number: stored.number, owned: false };
            case VALUE.OBJECT:
                return { type: VALUE.OBJECT, entity: stored.entity, owned: false };
            case VALUE.ARRAY:
                return { type: VALUE.ARRAY, entity: stored.entity, owned: false };
            default:
                return { type: VALUE.UNKNOWN };
        }
    }

    valueToFloat(value) {
        if (value.type === VALUE.FLOAT || value.type === VALUE.INTEGER) {
            return value.number;
        }
        return null;
    }

    valueToInteger(value) {
        if (value.type === VALUE.INTEGER) {
            return Math.trunc(value.number);
        }
        if (value.type === VALUE.FLOAT && isIntegerNumber(value.number)) {
            return Math.trunc(value.number);
        }
        return null;
    }

    valueToBoolean(value) {
        const numeric = this.valueToFloat(value);
        if (numeric == null) {
            return null;
        }
        return numeric !== 0;
    }

    valueToString(value) {
        if (value.type === VALUE.STRING) {
            return { text: value.text || "", ownedTempId: null };
        }
        if (value.type === VALUE.INTEGER) {
            const tempId = this.memory.allocate(32, "temp-int-string");
            return { text: `${Math.trunc(value.number)}`, ownedTempId: tempId };
        }
        if (value.type === VALUE.FLOAT) {
            const tempId = this.memory.allocate(64, "temp-float-string");
            return { text: Number(value.number).toFixed(6), ownedTempId: tempId };
        }
        return null;
    }

    markDynamicGap(message) {
        this.dynamicResolved = false;
        if (!this.notes.includes(message)) {
            this.notes.push(message);
        }
    }
}

function formatBytes(bytes) {
    if (bytes < 1024) {
        return `${bytes} B`;
    }
    if (bytes < 1024 * 1024) {
        return `${(bytes / 1024).toFixed(2)} KiB`;
    }
    return `${(bytes / (1024 * 1024)).toFixed(2)} MiB`;
}

function parseArguments(argv) {
    const options = {
        arch: "both",
        json: false,
        scriptPath: null,
    };

    for (let index = 2; index < argv.length; index++) {
        const arg = argv[index];
        if (arg === "--json") {
            options.json = true;
            continue;
        }
        if (arg === "--arch") {
            index++;
            if (index >= argv.length) {
                fail("Missing value after --arch");
            }
            options.arch = argv[index];
            continue;
        }
        if (arg.startsWith("--")) {
            fail(`Unknown option: ${arg}`);
        }
        if (options.scriptPath != null) {
            fail("Only one script path is accepted");
        }
        options.scriptPath = arg;
    }

    if (!options.scriptPath) {
        fail("Usage: node scripts/common/utils/estimate-e0-memory.js <script.e0> [--arch x86-32|x86-64|both] [--json]");
    }

    if (!["x86-32", "x86-64", "both"].includes(options.arch)) {
        fail("Invalid --arch value. Expected x86-32, x86-64, or both.");
    }

    return options;
}

function analyzeScript(scriptText, archName) {
    const parser = new Parser(scriptText);
    const ast = parser.parse();
    const simulator = new Simulator(ARCH_MODELS[archName], ast, parser.stats);
    return simulator.run();
}

function main() {
    const options = parseArguments(process.argv);
    const resolvedPath = path.resolve(options.scriptPath);
    const scriptText = fs.readFileSync(resolvedPath, "utf8");
    const architectures = options.arch === "both" ? ["x86-32", "x86-64"] : [options.arch];
    const results = {};

    for (const archName of architectures) {
        results[archName] = analyzeScript(scriptText, archName);
    }

    if (options.json) {
        process.stdout.write(`${JSON.stringify({
            script: resolvedPath,
            results,
        }, null, 2)}\n`);
        return;
    }

    process.stdout.write(`Script: ${resolvedPath}\n`);
    for (const archName of architectures) {
        const result = results[archName];
        process.stdout.write(`\n[${archName}]\n`);
        process.stdout.write(`Fixed interpreter: ${formatBytes(result.fixedInterpreterBytes)}\n`);
        process.stdout.write(`AST + parser-owned buffers: ${formatBytes(result.astBytes)}\n`);
        process.stdout.write(`Peak during parse + execution: ${formatBytes(result.peakBytes)}\n`);
        process.stdout.write(`Retained after execution: ${formatBytes(result.retainedBytesAfterExecution)}\n`);
        process.stdout.write(`Runtime simulation: ${result.dynamicResolved ? "resolved" : "partial"}\n`);
        process.stdout.write(`Live variables after execution: ${result.liveVariableCount}\n`);
        if (result.notes.length > 0) {
            process.stdout.write(`Notes:\n`);
            for (const note of result.notes) {
                process.stdout.write(`- ${note}\n`);
            }
        }
    }
}

main();
