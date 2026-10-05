import { readFileSync } from 'node:fs';

const TOKEN = /\s+|\/\/[^\n]*|\+=|-=|\*=|\/=|==|!=|<=|>=|&&|\|\||[A-Za-z_][A-Za-z0-9_]*|(?:\d+\.\d*|\.\d+|\d+)(?:[eE][+-]?\d+)?|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|[{}(),:;.=+*/<>!\[\]-]/gy;
const ANNOTATION = /^\/\/\s*@node\s+([A-Za-z][A-Za-z0-9_-]*)\s*$/;
const INT32_MIN = -2147483648;
const INT32_MAX = 2147483647;

export function parseBehavior(source) {
  const tokens = tokenize(source);
  let at = 0;
  const ids = new Set();
  const peek = (value) => tokens[at]?.value === value;
  const next = () => tokens[at++];
  const expect = (value) => {
    if (!peek(value)) fail(tokens[at], 'expected ' + value);
    return next();
  };
  const expectName = () => {
    if (tokens[at]?.kind !== 'name') fail(tokens[at], 'expected a name');
    return next();
  };
  const fail = (token, message, code = 'E_PARSE') => {
    const offset = token?.start ?? source.length;
    const error = new SyntaxError('line ' + lineAt(source, offset) + ': ' + message);
    error.code = code;
    error.span = { start: offset, end: token?.end ?? offset, line: lineAt(source, offset), column: columnAt(source, offset) };
    throw error;
  };
  const parseExpression = (terminator) => {
    const first = at;
    let depth = 0;
    while (at < tokens.length) {
      const value = tokens[at].value;
      if (depth === 0 && value === terminator) break;
      if (value === '(') depth++;
      if (value === ')') {
        if (depth === 0) fail(tokens[at], 'unbalanced expression');
        depth--;
      }
      at++;
    }
    if (at === first) fail(tokens[at], 'expected an expression');
    if (depth !== 0) fail(tokens[at], 'unbalanced expression');
    return {
      text: source.slice(tokens[first].start, tokens[at - 1].end),
      start: tokens[first].start,
      end: tokens[at - 1].end,
      line: lineAt(source, tokens[first].start),
      column: columnAt(source, tokens[first].start),
    };
  };
  const annotation = () => {
    const token = tokens[at];
    if (token?.kind !== 'annotation') return null;
    at++;
    if (tokens[at]?.kind === 'annotation') fail(tokens[at], 'one @node marker per statement');
    return token.value;
  };
  const parseBlock = () => {
    expect('{');
    const statements = [];
    while (at < tokens.length && !peek('}')) {
      const marker = annotation();
      if (marker && ids.has(marker)) fail(tokens[at], 'duplicate @node id ' + marker);
      if (marker) ids.add(marker);
      if (peek('if')) {
        next();
        expect('(');
        const condition = parseExpression(')');
        expect(')');
        const yes = parseBlock();
        let no = [];
        if (peek('else')) {
          next();
          no = parseBlock();
        }
        statements.push({ id: marker, marker, kind: 'if', condition: condition.text, span: condition, yes, no });
        continue;
      }
      if (peek('for')) {
        next();
        expect('(');
        const variable = expectName();
        if (!peek('in')) fail(tokens[at], "expected 'in' in array iteration");
        next();
        const array = expectName();
        expect(')');
        const body = parseBlock();
        statements.push({ id: marker, marker, kind: 'foreach', variable: variable.value, array: array.value,
          span: { start: variable.start, end: tokens[at - 1].end, line: lineAt(source, variable.start), column: columnAt(source, variable.start) }, body });
        continue;
      }
      const name = expectName();
      if (peek('.') && ['add', 'remove'].includes(tokens[at + 1]?.value)) {
        next();
        const method = expectName();
        expect('(');
        const argument = parseExpression(')');
        expect(')');
        expect(';');
        statements.push({ id: marker, marker, kind: 'set_mutation', set: name.value, method: method.value,
          argument: argument.text, argumentSpan: argument,
          span: { start: name.start, end: argument.end, line: lineAt(source, name.start), column: columnAt(source, name.start) } });
        continue;
      }
      const targetParts = [name.value];
      let targetEnd = name.end;
      while (peek('.')) {
        next();
        const member = expectName();
        targetParts.push('.', member.value);
        targetEnd = member.end;
      }
      if (peek('+=') || peek('-=') || peek('*=') || peek('/=') || peek('=')) {
        const operator = next();
        const expression = parseExpression(';');
        expect(';');
        const targetSpan = { start: name.start, end: targetEnd, line: lineAt(source, name.start), column: columnAt(source, name.start) };
        statements.push({ id: marker, marker, kind: 'expression',
          source: targetParts.join('') + ' ' + operator.value + ' ' + expression.text,
          target: targetParts.join(''), targetSource: source.slice(name.start, targetEnd), targetSpan,
          operatorSource: source.slice(targetEnd, expression.start), rhs: expression.text, rhsSpan: expression,
          span: { start: name.start, end: expression.end, line: lineAt(source, name.start), column: columnAt(source, name.start) } });
        continue;
      }
      if (name.value === 'destroy') {
        expect('(');
        const target = expectName();
        expect(')');
        expect(';');
        if (target.value !== 'self') fail(target, 'the prototype supports destroy(self)');
        statements.push({ id: marker, marker, kind: 'destroy' });
        continue;
      }
      fail(name, 'unsupported statement or engine call ' + name.value);
    }
    expect('}');
    const branch = statements.findIndex((statement) => statement.kind === 'if');
    if (branch >= 0 && branch !== statements.length - 1) {
      fail(tokens[at - 1], 'an if/else must be the final statement in its block in this prototype');
    }
    return statements;
  };

  expect('behavior');
  const name = expectName().value;
  expect('{');
  const handlers = [];
  const handlerNames = new Set();
  const stateVariables = [];
  const stateNames = new Set();
  while (at < tokens.length && !peek('}')) {
    if (peek('state')) {
      const stateToken = next();
      const type = expectName();
      let container;
      let elementType;
      if (type.value === 'set') {
        container = 'set';
        expect('<');
        elementType = expectName();
        expect('>');
      } else {
        container = 'array';
        elementType = type;
        if (!peek('[')) fail(tokens[at], 'state values in this slice must be typed arrays or typed sets');
        next();
        expect(']');
      }
      const nameToken = expectName();
      if (stateNames.has(nameToken.value)) fail(nameToken, 'duplicate state variable ' + nameToken.value, 'E_STATE_DUPLICATE');
      stateNames.add(nameToken.value);
      expect('=');
      expect('[');
      const values = [];
      while (!peek(']')) {
        const signToken = peek('-') ? next() : null;
        const sign = signToken ? -1 : 1;
        const valueToken = next();
        const literalToken = signToken ? { ...valueToken, start: signToken.start } : valueToken;
        let value;
        if (/^(?:\d+\.\d*|\.\d+|\d+)(?:[eE][+-]?\d+)?$/.test(valueToken.value)) value = sign * Number(valueToken.value);
        else if (sign === 1 && (valueToken.value === 'true' || valueToken.value === 'false')) value = valueToken.value === 'true';
        else if (sign === 1 && (/^"/.test(valueToken.value) || /^'/.test(valueToken.value))) value = decodeStringLiteral(valueToken, fail);
        else fail(valueToken, 'state array defaults must be scalar literals');
        if (elementType.value === 'int' && (!Number.isInteger(value) || value < INT32_MIN || value > INT32_MAX)) {
          fail(literalToken, 'int collection defaults must be signed 32-bit integers');
        }
        if (elementType.value === 'float' &&
            (typeof value !== 'number' || !Number.isFinite(value) || !Number.isFinite(Math.fround(value)))) {
          fail(literalToken, 'float collection defaults must be finite float32 literals');
        }
        if (elementType.value === 'bool' && typeof value !== 'boolean') fail(valueToken, 'bool collections require boolean literals');
        if (elementType.value === 'string' && typeof value !== 'string') fail(valueToken, 'string collections require string literals');
        values.push(value);
        if (!peek(',')) break;
        next();
        if (peek(']')) break;
      }
      expect(']');
      expect(';');
      if (!['int', 'float', 'bool', 'string', 'entity'].includes(elementType.value)) fail(elementType, 'unsupported collection element type ' + elementType.value);
      if (elementType.value === 'entity' && container !== 'set') fail(elementType, 'entity handles are supported only in sets in this slice', 'E_COLLECTION_ELEMENT_TYPE');
      if (elementType.value === 'entity' && values.length) fail(elementType, 'entity collection defaults must be empty; entity handles come from event payloads');
      stateVariables.push({ name: nameToken.value, type: elementType.value, container, default: values,
        span: { start: stateToken.start, end: tokens[at - 1].end, line: lineAt(source, stateToken.start), column: columnAt(source, stateToken.start) } });
      continue;
    }
    expect('on');
    const eventToken = expectName();
    const event = eventToken.value;
    if (handlerNames.has(event)) fail(tokens[at - 1], 'duplicate handler for ' + event);
    handlerNames.add(event);
    expect('(');
    const params = [];
    const paramNames = new Set();
    while (!peek(')')) {
      const type = expectName();
      const alias = expectName();
      if (paramNames.has(alias.value)) fail(alias, 'duplicate handler input ' + alias.value, 'E_COMPONENT_ALIAS_DUPLICATE');
      paramNames.add(alias.value);
      params.push({ kind: 'component', component: type.value, name: alias.value,
        typeSpan: { start: type.start, end: type.end, line: lineAt(source, type.start), column: columnAt(source, type.start) },
        aliasSpan: { start: alias.start, end: alias.end, line: lineAt(source, alias.start), column: columnAt(source, alias.start) } });
      if (!peek(',')) break;
      next();
    }
    expect(')');
    const body = parseBlock();
    handlers.push({ event, eventSpan: { start: eventToken.start, end: eventToken.end, line: lineAt(source, eventToken.start), column: columnAt(source, eventToken.start) }, params, body });
  }
  expect('}');
  if (at !== tokens.length) fail(tokens[at], 'unexpected content after behavior');
  if (!handlers.length) fail(tokens[0], 'behavior must define at least one synchronous handler');
  const behavior = { name, handlers, stateVariables };
  behavior.signature = JSON.stringify(shapeOf(behavior));
  return behavior;
}

export class BehaviorCodeError extends Error {
  constructor(code, message, span = { start: 0, end: 0, line: 1, column: 1 }) {
    super(message);
    this.name = 'BehaviorCodeError';
    this.code = code;
    this.span = span;
  }
}

function tokenize(source) {
  const result = [];
  let offset = 0;
  while (offset < source.length) {
    TOKEN.lastIndex = offset;
    const match = TOKEN.exec(source);
    if (!match) throw new BehaviorCodeError('E_PARSE', 'line ' + lineAt(source, offset) + ': unsupported character ' + JSON.stringify(source[offset]), {
      start: offset, end: offset + 1, line: lineAt(source, offset), column: columnAt(source, offset),
    });
    const value = match[0];
    offset = TOKEN.lastIndex;
    if (/^\s+$/.test(value)) continue;
    if (value.startsWith('//')) {
      const marked = ANNOTATION.exec(value);
      if (marked) result.push({ kind: 'annotation', value: marked[1], start: match.index, end: offset });
      continue;
    }
    result.push({ kind: /^[A-Za-z_]/.test(value) ? 'name' : 'symbol', value, start: match.index, end: offset });
  }
  return result;
}

function decodeStringLiteral(token, fail) {
  const quote = token.value[0];
  const body = token.value.slice(1, -1);
  let result = '';
  for (let i = 0; i < body.length; i++) {
    if (body[i] !== '\\') {
      result += body[i];
      continue;
    }
    const escape = body[++i];
    const simple = { '"': '"', "'": "'", '\\': '\\', b: '\b', f: '\f', n: '\n', r: '\r', t: '\t', v: '\v', '0': '\0' };
    if (Object.hasOwn(simple, escape)) {
      result += simple[escape];
      continue;
    }
    if (escape === 'u') {
      const rest = body.slice(i + 1);
      const braced = /^\{([0-9A-Fa-f]{1,6})\}/.exec(rest);
      const fixed = /^([0-9A-Fa-f]{4})/.exec(rest);
      const digits = braced?.[1] ?? fixed?.[1];
      const width = braced ? braced[0].length : fixed?.[0].length;
      if (digits) {
        const codepoint = Number.parseInt(digits, 16);
        if (codepoint <= 0x10ffff) {
          result += String.fromCodePoint(codepoint);
          i += width;
          continue;
        }
      }
    }
    fail(token, `unsupported string escape \\${escape}`, 'E_STRING_ESCAPE');
  }
  if (quote !== '"' && quote !== "'") fail(token, 'invalid string literal');
  if (result.includes('\0')) fail(token, 'string literals cannot contain NUL characters', 'E_STRING_NUL');
  return result;
}

function lineAt(source, offset) {
  let line = 1;
  for (let i = 0; i < offset; i++) if (source[i] === '\n') line++;
  return line;
}

function columnAt(source, offset) {
  const previous = source.lastIndexOf('\n', Math.max(0, offset - 1));
  return offset - previous;
}

function flatten(statements) {
  const result = [];
  for (const statement of statements) {
    result.push(statement);
    if (statement.kind === 'if') result.push(...flatten(statement.yes), ...flatten(statement.no));
    if (statement.kind === 'foreach') result.push(...flatten(statement.body));
  }
  return result;
}

function expressionSymbols(source) {
  const tokens = tokenize(source);
  const symbols = new Set();
  for (let i = 0; i < tokens.length; i++) {
    const token = tokens[i];
    if (token.kind !== 'name' || token.value === 'true' || token.value === 'false') continue;
    if (tokens[i + 1]?.value === '(' || tokens[i - 1]?.value === '.') continue;
    symbols.add(token.value);
  }
  return [...symbols];
}

const RESERVED = new Set(['behavior', 'on', 'if', 'else', 'for', 'while', 'state', 'event', 'self', 'true', 'false', 'int', 'float', 'bool', 'string', 'set']);
const HOST_EVENT_NAMES = new Map([['Contact Begin', 'ContactBegin']]);
const HOST_EVENT_PAYLOAD_ALIASES = new Map([
  ['DeltaSeconds', 'dt'], ['Direction', 'direction'], ['Other', 'other'], ['Point', 'point'],
]);

function failResolution(code, message, span = {}) {
  throw new BehaviorCodeError(code, message, {
    start: span.start ?? 0, end: span.end ?? span.start ?? 0,
    line: span.line ?? 1, column: span.column ?? 1,
  });
}

function shiftedSpan(span, source, start, end) {
  const prefix = source.slice(0, start);
  const lines = prefix.split('\n');
  return {
    start: span.start + start,
    end: span.start + end,
    line: span.line + lines.length - 1,
    column: lines.length === 1 ? span.column + start : lines[lines.length - 1].length + 1,
  };
}

function schemaComponent(registry, sourceName) {
  return (registry.components ?? []).find((entry) => entry.name === sourceName || entry.key === sourceName || entry.type === sourceName);
}

function eventSourceName(displayName) {
  return HOST_EVENT_NAMES.get(displayName) ?? displayName;
}

function payloadAlias(payload) {
  return payload.alias ?? HOST_EVENT_PAYLOAD_ALIASES.get(payload.pin) ?? payload.pin[0].toLowerCase() + payload.pin.slice(1);
}

function replaceRanges(source, edits) {
  let output = source;
  edits.sort((a, b) => b.start - a.start);
  for (const edit of edits) output = output.slice(0, edit.start) + edit.text + output.slice(edit.end);
  return output;
}

/* Resolve names against the reflected engine schema. The returned internal
 * expressions remain ordinary Behavior Expression source; event pins and
 * component fields are represented by generated scalar input names. */
export function resolveBehavior(behavior, registry) {
  const componentNamesByKey = new Map((registry.components ?? []).map((entry) => [entry.key, entry.name]));
  const knownEvents = new Map((registry.events ?? []).map((entry) => [entry.source_name ?? eventSourceName(entry.name), entry]));
  const stateVariables = new Map();
  for (const value of behavior.stateVariables ?? []) {
    if (RESERVED.has(value.name)) failResolution('E_NAME_RESERVED', `state name '${value.name}' is reserved`, value.span);
    stateVariables.set(value.name, value);
  }
  for (const handler of behavior.handlers) {
    const event = knownEvents.get(handler.event);
    if (!event) failResolution('E_EVENT_UNRESOLVED', `unknown event '${handler.event}'`, handler.eventSpan);
    const aliases = new Map();
    const componentTypes = new Set();
    for (const parameter of handler.params) {
      if (RESERVED.has(parameter.name)) failResolution('E_NAME_RESERVED', `component alias '${parameter.name}' is reserved`, parameter.aliasSpan);
      const component = schemaComponent(registry, parameter.component);
      if (!component) failResolution('E_COMPONENT_UNRESOLVED', `unknown reflected component '${parameter.component}'`, parameter.typeSpan);
      if (aliases.has(parameter.name)) failResolution('E_COMPONENT_ALIAS_DUPLICATE', `duplicate component alias '${parameter.name}'`, parameter.aliasSpan);
      if (componentTypes.has(component.key)) failResolution('E_COMPONENT_CONTEXT_DUPLICATE', `component '${component.name}' is declared more than once in this handler`, parameter.typeSpan);
      componentTypes.add(component.key);
      parameter.componentKey = component.key;
      parameter.componentName = component.name;
      parameter.kind = 'component';
      aliases.set(parameter.name, { parameter, component });
      if (stateVariables.has(parameter.name)) failResolution('E_NAME_COLLISION_STATE', `component alias '${parameter.name}' conflicts with a state collection`, parameter.aliasSpan);
    }

    const fieldsFor = (name) => [...aliases.values()].flatMap(({ parameter, component }) => {
      const match = (component.fields ?? []).find((field) => field.name === name);
      return match ? [{ alias: parameter.name, parameter, component, field: match }] : [];
    });
    const eventFields = new Map();
    const componentFields = new Map();
    const componentInput = (alias, field) => {
      const parameterIndex = handler.params.indexOf(alias.parameter);
      const internal = `_bc_component_${parameterIndex}_${field.name}`;
      if (!componentFields.has(internal)) {
        componentFields.set(internal, {
          name: internal, alias: alias.parameter.name, componentKey: alias.component.key,
          field: field.name, type: field.type,
        });
      }
      return internal;
    };
    const resolveExpression = (source, span, assignmentTarget = null, locals = []) => {
      const parts = tokenize(source);
      const tokenSpan = (token, end = token.end) => shiftedSpan(span, source, token.start, end);
      const edits = [];
      const eventInputs = new Set();
      const componentInputs = new Map();
      for (let i = 0; i < parts.length; i++) {
        const token = parts[i];
        if (token.kind !== 'name' || parts[i - 1]?.value === '.' || parts[i + 1]?.value === '(') continue;
        const isMember = parts[i + 1]?.value === '.';
        if (isMember) {
          const member = parts[i + 2];
          if (member?.kind !== 'name') continue;
          if (token.value === 'event') {
            if (parts[i + 3]?.value === '.') failResolution('E_EVENT_NESTED_MEMBER_UNSUPPORTED', 'event payload members must be used as one direct value in this slice', tokenSpan(token, parts[i + 4]?.end ?? parts[i + 3].end));
            const payload = (event.payload ?? []).find((entry) => payloadAlias(entry) === member.value);
            if (!payload) failResolution('E_EVENT_FIELD_NOT_FOUND', `event '${handler.event}' has no payload field '${member.value}'`, tokenSpan(token, member.end));
            if (assignmentTarget === token.value + '.' + member.value) {
              failResolution('E_ASSIGNMENT_READ_ONLY', `event field '${member.value}' is read-only`, tokenSpan(token, member.end));
            }
            const internal = `_bc_event_${payload.pin.replace(/[^A-Za-z0-9_]/g, '_')}`;
            edits.push({ start: token.start, end: member.end, text: internal });
            eventInputs.add(internal);
            eventFields.set(internal, { alias: member.value, pin: payload.pin, type: payload.type });
            i += 2;
            continue;
          }
          const alias = aliases.get(token.value);
          if (alias) {
            if (parts[i + 3]?.value === '.') failResolution('E_COMPONENT_NESTED_MEMBER_UNSUPPORTED', 'nested component member access is not supported by this slice', tokenSpan(token, parts[i + 4]?.end ?? parts[i + 3].end));
            const reflected = (alias.component.fields ?? []).find((field) => field.name === member.value);
            if (!reflected) failResolution('E_COMPONENT_FIELD_NOT_FOUND', `component '${alias.component.name}' has no field '${member.value}'`, tokenSpan(token, member.end));
            if (assignmentTarget === token.value + '.' + member.value && reflected.writable === false) {
              failResolution('E_ASSIGNMENT_READ_ONLY', `component field '${member.value}' is read-only`, tokenSpan(token, member.end));
            }
            const internal = componentInput(alias, reflected);
            componentInputs.set(internal, componentFields.get(internal));
            edits.push({ start: token.start, end: member.end, text: internal });
            i += 2;
            continue;
          }
          const state = stateVariables.get(token.value);
          if (state) failResolution('E_SET_METHOD_UNSUPPORTED', `collection '${token.value}' only supports add, remove and contains in this slice`, tokenSpan(token, member.end));
          const local = locals.find((entry) => entry.name === token.value);
          if (local) failResolution('E_LOOP_MEMBER_UNSUPPORTED', `member access on loop value '${token.value}' is not supported by this slice`, tokenSpan(token, member.end));
          // Preserve member reads rooted in a registered pure function result;
          // calls are skipped above; other unqualified roots are unresolved.
          if (token.value === 'self') failResolution('E_SELF_REQUIRES_ENGINE_CALL', 'self is only available to registered engine calls in this slice', tokenSpan(token, member.end));
          failResolution('E_NAME_UNRESOLVED', `unresolved member root '${token.value}'`, tokenSpan(token, member.end));
        }
        if (token.value === 'event') failResolution('E_EVENT_REQUIRES_MEMBER', 'event must be followed by a registered payload field', tokenSpan(token));
        const local = locals.find((entry) => entry.name === token.value);
        if (local) {
          if (assignmentTarget === token.value) failResolution('E_LOOP_BINDING_READ_ONLY', `loop binding '${token.value}' is read-only`, tokenSpan(token));
          continue;
        }
        if (RESERVED.has(token.value) || token.value === 'pi' || token.value === 'e') continue;
        const alias = aliases.get(token.value);
        const fieldMatches = fieldsFor(token.value);
        if (alias && fieldMatches.length) failResolution('E_NAME_COLLISION_ALIAS_FIELD', `'${token.value}' is both a component alias and a field; qualify the field`, tokenSpan(token));
        if (alias) failResolution('E_COMPONENT_ALIAS_REQUIRES_MEMBER', `component alias '${token.value}' requires a field name`, tokenSpan(token));
        if (fieldMatches.length > 1) failResolution('E_NAME_AMBIGUOUS_COMPONENT_FIELD', `field '${token.value}' exists on multiple declared components; qualify it`, tokenSpan(token));
        if (fieldMatches.length === 1) {
          const candidate = fieldMatches[0];
          if (assignmentTarget === token.value && candidate.field.writable === false) failResolution('E_ASSIGNMENT_READ_ONLY', `component field '${token.value}' is read-only`, tokenSpan(token));
          const internal = componentInput(candidate, candidate.field);
          componentInputs.set(internal, componentFields.get(internal));
          edits.push({ start: token.start, end: token.end, text: internal });
          continue;
        }
        if (assignmentTarget === token.value && token.value === 'self') failResolution('E_ASSIGNMENT_READ_ONLY', 'self is read-only', tokenSpan(token));
        failResolution('E_NAME_UNRESOLVED', `unresolved name '${token.value}'; component fields and event data must be qualified`, tokenSpan(token));
      }
      return { source: replaceRanges(source, edits), eventInputs: [...eventInputs], componentInputs: [...componentInputs.values()] };
    };

    const setOperand = (source, span, locals) => {
      const operandName = source.trim();
      const local = locals.find((entry) => entry.name === operandName);
      if (local && /^[A-Za-z_][A-Za-z0-9_]*$/.test(operandName)) return { ...local, kind: 'loop' };

      const match = /^event\.([A-Za-z_][A-Za-z0-9_]*)$/.exec(operandName);
      if (!match) failResolution('E_SET_ARGUMENT_UNSUPPORTED', 'set methods take a visible array element or direct event payload field', span);
      const resolved = resolveExpression(operandName, span, null, locals);
      const internal = resolved.eventInputs[0];
      const field = eventFields.get(internal);
      const elementType = field?.type === 'ecs.entity' ? 'entity'
        : field?.type === 'float' ? 'float'
        : field?.type === 'int' ? 'int'
        : field?.type === 'bool' ? 'bool'
        : ['const char *', 'string'].includes(field?.type) ? 'string' : null;
      if (!elementType) failResolution('E_SET_ARGUMENT_TYPE_UNSUPPORTED', `event payload '${match[1]}' is not a supported set element type`, span);
      return { kind: 'event', name: internal, pin: field.pin, elementType, source: resolved.source };
    };

    const expressions = (statements, locals = []) => {
      for (const statement of statements) {
        statement.loopBindings = locals;
        if (statement.kind === 'expression') {
          const target = statement.target;
          const leftResolved = resolveExpression(statement.targetSource, statement.targetSpan, target, locals);
          const rightResolved = resolveExpression(statement.rhs, statement.rhsSpan, null, locals);
          statement.resolvedSource = leftResolved.source + statement.operatorSource + rightResolved.source;
          statement.eventInputs = [...new Set([...leftResolved.eventInputs, ...rightResolved.eventInputs])];
          statement.componentInputs = [...new Map([...leftResolved.componentInputs, ...rightResolved.componentInputs].map((input) => [input.name, input])).values()];
        } else if (statement.kind === 'if') {
          const match = /^([A-Za-z_][A-Za-z0-9_]*)\s*\.\s*contains\s*\((.*)\)$/.exec(statement.condition);
          if (match) {
            const set = stateVariables.get(match[1]);
            if (!set || set.container !== 'set') failResolution('E_SET_UNRESOLVED', `unknown typed set '${match[1]}'`, statement.span);
            const rawArgument = match[2];
            const open = statement.condition.indexOf('(', match.index);
            const argumentStart = statement.condition.indexOf(rawArgument, open + 1) + rawArgument.length - rawArgument.trimStart().length;
            const argument = rawArgument.trim();
            const operandSpan = shiftedSpan(statement.span, statement.condition, argumentStart, argumentStart + argument.length);
            const operand = setOperand(argument, operandSpan, locals);
            if (operand.elementType !== set.type) failResolution('E_SET_ELEMENT_TYPE', `set item type '${operand.elementType}' does not match set element type '${set.type}'`, operandSpan);
            statement.conditionSet = { name: match[1], descriptor: set, operand };
            statement.eventInputs = operand.kind === 'event' ? [operand.name] : [];
            statement.componentInputs = [];
          } else {
            const resolved = resolveExpression(statement.condition, statement.span, null, locals);
            statement.resolvedCondition = resolved.source;
            statement.eventInputs = resolved.eventInputs;
            statement.componentInputs = resolved.componentInputs;
          }
          expressions(statement.yes, locals);
          expressions(statement.no, locals);
        } else if (statement.kind === 'foreach') {
          const array = stateVariables.get(statement.array);
          if (!array) failResolution('E_ARRAY_UNRESOLVED', `unknown state array '${statement.array}'`, statement.span);
          if (array.container !== 'array') failResolution('E_ITERATION_REQUIRES_ARRAY', `foreach requires an array; '${statement.array}' is not an array`, statement.span);
          if (RESERVED.has(statement.variable) || aliases.has(statement.variable) || stateVariables.has(statement.variable) || locals.some((entry) => entry.name === statement.variable)) {
            failResolution('E_LOOP_BINDING_COLLISION', `loop binding '${statement.variable}' conflicts with a visible name`, statement.span);
          }
          statement.elementType = array.type;
          const loop = { name: statement.variable, array: statement.array, elementType: array.type, statement };
          statement.loopBindings = [...locals, loop];
          expressions(statement.body, [...locals, loop]);
        } else if (statement.kind === 'set_mutation') {
          const set = stateVariables.get(statement.set);
          if (!set || set.container !== 'set') failResolution('E_SET_UNRESOLVED', `unknown typed set '${statement.set}'`, statement.span);
          const operand = setOperand(statement.argument, statement.argumentSpan, locals);
          if (operand.elementType !== set.type) failResolution('E_SET_ELEMENT_TYPE', `set item type '${operand.elementType}' does not match set element type '${set.type}'`, statement.argumentSpan);
          statement.setDescriptor = set;
          statement.setOperand = operand;
          statement.eventInputs = operand.kind === 'event' ? [operand.name] : [];
        }
      }
    };
    expressions(handler.body);
    handler.eventFields = Object.fromEntries(eventFields);
    handler.componentFields = Object.fromEntries(componentFields);
    handler.eventOutputs = Object.fromEntries([...eventFields].map(([internal, value]) => [internal, value.pin]));
    handler.eventSourceName = event.name;
  }
  behavior.componentNamesByKey = Object.fromEntries(componentNamesByKey);
  behavior.signature = JSON.stringify(shapeOf(behavior));
  return behavior;
}

function shapeOf(behavior) {
  return {
    name: behavior.name,
    stateVariables: (behavior.stateVariables ?? []).map(({ name, type, container, default: value }) => ({ name, type, container, default: value })),
    handlers: behavior.handlers.map((handler) => ({
      event: handler.event,
      body: handler.body.map(shapeStatement),
    })),
  };
}

function shapeStatement(statement) {
  if (statement.kind === 'if') return { id: statement.id, kind: statement.conditionSet ? 'if_set' : 'if', yes: statement.yes.map(shapeStatement), no: statement.no.map(shapeStatement) };
  if (statement.kind === 'foreach') return { id: statement.id, kind: 'foreach', array: statement.array, variable: statement.variable, body: statement.body.map(shapeStatement) };
  if (statement.kind === 'set_mutation') return { id: statement.id, kind: 'set_mutation', set: statement.set, method: statement.method, argument: statement.argument };
  return { id: statement.id, kind: statement.kind };
}

function statementKey(statement) {
  if (statement.kind === 'expression') return 'expression:' + statement.source;
  if (statement.kind === 'if') return 'if:' + statement.condition;
  if (statement.kind === 'foreach') return `foreach:${statement.variable} in ${statement.array}`;
  if (statement.kind === 'set_mutation') return `set:${statement.set}.${statement.method}(${statement.argument})`;
  return statement.kind;
}

function snapshotBlock(statements) {
  return statements.map((statement) => ({
    ref: statement.id,
    marker: statement.marker ?? null,
    kind: statement.kind,
    key: statementKey(statement),
    yes: statement.kind === 'if' ? snapshotBlock(statement.yes) : undefined,
    no: statement.kind === 'if' ? snapshotBlock(statement.no) : undefined,
    body: statement.kind === 'foreach' ? snapshotBlock(statement.body) : undefined,
    condition: statement.kind === 'if' ? statement.condition : undefined,
    set: statement.kind === 'set_mutation' ? statement.set : undefined,
  }));
}

function sameBlockSemantics(oldBlock, statements) {
  return oldBlock.length === statements.length && oldBlock.every((old, index) => {
    const statement = statements[index];
    return old.kind === statement.kind && old.key === statementKey(statement) && (
      statement.kind !== 'if' || (
        sameBlockSemantics(old.yes ?? [], statement.yes) && sameBlockSemantics(old.no ?? [], statement.no)
      )
    ) && (
      statement.kind !== 'foreach' || sameBlockSemantics(old.body ?? [], statement.body)
    );
  });
}

function assignFreshIds(statements, allocate) {
  for (const statement of statements) {
    statement.id = statement.marker || allocate();
    if (statement.kind === 'if') {
      assignFreshIds(statement.yes, allocate);
      assignFreshIds(statement.no, allocate);
    }
    if (statement.kind === 'foreach') assignFreshIds(statement.body, allocate);
  }
}

function reconcileBlock(oldBlock, statements, allocate) {
  if (!oldBlock) {
    assignFreshIds(statements, allocate);
    return;
  }
  if (sameBlockSemantics(oldBlock, statements)) {
    const usedOld = new Set();
    const pairs = new Map();
    for (let newIndex = 0; newIndex < statements.length; newIndex++) {
      const marker = statements[newIndex].marker;
      if (!marker) continue;
      const oldIndex = oldBlock.findIndex((old, index) => !usedOld.has(index) && (old.marker === marker || old.ref === marker));
      if (oldIndex >= 0) {
        pairs.set(newIndex, oldIndex);
        usedOld.add(oldIndex);
      }
    }
    const remainingOld = oldBlock.map((_, index) => index).filter((index) => !usedOld.has(index));
    const remainingNew = statements.map((_, index) => index).filter((index) => !pairs.has(index));
    for (let i = 0; i < remainingNew.length; i++) pairs.set(remainingNew[i], remainingOld[i]);
    for (const [newIndex, oldIndex] of pairs) {
      const statement = statements[newIndex];
      statement.id = oldBlock[oldIndex].ref;
      if (statement.kind === 'if') {
        reconcileBlock(oldBlock[oldIndex].yes ?? [], statement.yes, allocate);
        reconcileBlock(oldBlock[oldIndex].no ?? [], statement.no, allocate);
      }
      if (statement.kind === 'foreach') reconcileBlock(oldBlock[oldIndex].body ?? [], statement.body, allocate);
    }
    return;
  }

  const pairs = new Map();
  const usedOld = new Set();
  const pair = (newIndex, oldIndex) => {
    if (pairs.has(newIndex) || usedOld.has(oldIndex)) throw new Error('source node identity is ambiguous; use unique // @node names on the affected statements');
    pairs.set(newIndex, oldIndex);
    usedOld.add(oldIndex);
  };

  // Existing explicit labels remain authoritative when a statement is edited or moved.
  for (let newIndex = 0; newIndex < statements.length; newIndex++) {
    const marker = statements[newIndex].marker;
    if (!marker) continue;
    const oldIndex = oldBlock.findIndex((old, index) => !usedOld.has(index) && (old.marker === marker || old.ref === marker));
    if (oldIndex >= 0) pair(newIndex, oldIndex);
  }

  const counts = (entries, keyOf, excluded) => {
    const result = new Map();
    entries.forEach((entry, index) => {
      if (excluded.has(index)) return;
      const key = keyOf(entry);
      result.set(key, (result.get(key) ?? 0) + 1);
    });
    return result;
  };
  const oldCounts = counts(oldBlock, (entry) => entry.key, usedOld);
  const newCounts = counts(statements, statementKey, new Set(pairs.keys()));
  for (const [key, count] of oldCounts) {
    if (count > 1 || (newCounts.get(key) ?? 0) > 1) {
      throw new Error('repeated unmarked statements make identity ambiguous; add distinct // @node names before editing this block');
    }
  }
  for (const [key, count] of newCounts) {
    if (count > 1 || (oldCounts.get(key) ?? 0) > 1) {
      throw new Error('repeated unmarked statements make identity ambiguous; add distinct // @node names before editing this block');
    }
  }

  // Exact syntax matches survive moves; unique remaining same-kind nodes are in-place edits.
  for (const [key, oldCount] of oldCounts) {
    if (oldCount !== 1 || newCounts.get(key) !== 1) continue;
    const oldIndex = oldBlock.findIndex((entry, index) => !usedOld.has(index) && entry.key === key);
    const newIndex = statements.findIndex((entry, index) => !pairs.has(index) && statementKey(entry) === key);
    pair(newIndex, oldIndex);
  }
  const kinds = new Set([
    ...oldBlock.filter((_, index) => !usedOld.has(index)).map((entry) => entry.kind),
    ...statements.filter((_, index) => !pairs.has(index)).map((entry) => entry.kind),
  ]);
  for (const kind of kinds) {
    const oldIndices = oldBlock.map((entry, index) => entry.kind === kind && !usedOld.has(index) ? index : -1).filter((index) => index >= 0);
    const newIndices = statements.map((entry, index) => entry.kind === kind && !pairs.has(index) ? index : -1).filter((index) => index >= 0);
    if (oldIndices.length === 1 && newIndices.length === 1) pair(newIndices[0], oldIndices[0]);
    else if (oldIndices.length > 0 && newIndices.length > 0) {
      throw new Error('multiple changed statements could share graph identities; add unique // @node names to the affected statements');
    }
  }

  for (let newIndex = 0; newIndex < statements.length; newIndex++) {
    const statement = statements[newIndex];
    const oldIndex = pairs.get(newIndex);
    if (oldIndex === undefined) statement.id = statement.marker || allocate();
    else statement.id = oldBlock[oldIndex].ref;
    if (statement.kind === 'if') {
      const old = oldIndex === undefined || oldBlock[oldIndex].kind !== 'if' ? null : oldBlock[oldIndex];
      reconcileBlock(old?.yes ?? null, statement.yes, allocate);
      reconcileBlock(old?.no ?? null, statement.no, allocate);
    }
    if (statement.kind === 'foreach') {
      const old = oldIndex === undefined || oldBlock[oldIndex].kind !== 'foreach' ? null : oldBlock[oldIndex];
      reconcileBlock(old?.body ?? null, statement.body, allocate);
    }
  }
}

export function reconcileSourceIdentities(behavior, previousMap = null) {
  const oldTree = previousMap?.source_tree;
  if (previousMap && !oldTree) {
    const markerIds = new Set(behavior.handlers.flatMap((handler) => flatten(handler.body).map((statement) => statement.marker).filter(Boolean)));
    const previousStatementRefs = Object.keys(previousMap.refs ?? {}).filter((ref) => {
      return previousMap.types?.[ref] !== 'Event' && previousMap.types?.[ref] !== 'Self';
    });
    if (previousStatementRefs.some((ref) => !markerIds.has(ref))) {
      throw new Error('this source map predates annotation-free identity metadata; compile once with its existing // @node markers, then remove or edit them');
    }
  }
  let next = Number.isSafeInteger(previousMap?.next_node_id) ? previousMap.next_node_id : 1;
  const reserved = new Set(flatten(behavior.handlers.flatMap((handler) => handler.body)).map((statement) => statement.marker).filter(Boolean));
  const allocated = new Set();
  const allocate = () => {
    let id;
    do { id = 'source-' + next++; } while (reserved.has(id) || allocated.has(id));
    allocated.add(id);
    return id;
  };
  const oldHandlers = new Map((oldTree?.handlers ?? []).map((handler) => [handler.event, handler]));
  for (const handler of behavior.handlers) {
    const old = oldHandlers.get(handler.event);
    reconcileBlock(old?.body ?? null, handler.body, allocate);
  }
  const refs = new Set();
  for (const handler of behavior.handlers) {
    for (const statement of flatten(handler.body)) {
      if (refs.has(statement.id)) throw new SyntaxError('source node identity is not unique: ' + statement.id);
      refs.add(statement.id);
    }
  }
  behavior.signature = JSON.stringify(shapeOf(behavior));
  return {
    next_node_id: next,
    source_tree: {
      handlers: behavior.handlers.map((handler) => ({ event: handler.event, body: snapshotBlock(handler.body) })),
    },
  };
}

export function sourceGraphShape(behavior) {
  const nodes = [];
  for (const state of behavior.stateVariables ?? []) nodes.push({ ref: stateGetRef(state.name), type: 'Get Variable', variable: state.name });
  for (const handler of behavior.handlers) {
    nodes.push({ ref: eventRef(handler.event), type: 'Event', event: handler.eventSourceName ?? handler.event });
    for (const statement of flatten(handler.body)) {
      if (statement.kind === 'if' && statement.conditionSet) {
        nodes.push({ ref: setConditionRef(statement.id), type: 'Set Contains' });
      }
      if (statement.kind === 'set_mutation') {
        nodes.push({ ref: setMutationRef(statement.id), type: statement.method === 'add' ? 'Set Add' : 'Set Remove' });
        if (statement.method === 'add') nodes.push({ ref: setMutationSourceRef(statement.id), type: 'Get Variable', variable: statement.set });
      }
      const type = statement.kind === 'destroy' ? 'Destroy Entity'
        : statement.kind === 'foreach' ? 'ForEach'
        : statement.kind === 'set_mutation' ? 'Set Variable'
        : statement.kind === 'if' && statement.conditionSet ? 'Branch'
        : 'Behavior Expression';
      nodes.push({ ref: statement.id, type, ...(statement.kind === 'set_mutation' ? { variable: statement.set } : {}) });
    }
  }
  if (nodes.some((node) => node.type === 'Destroy Entity')) nodes.push({ ref: 'self', type: 'Self' });
  const unique = new Set();
  for (const node of nodes) {
    if (unique.has(node.ref)) throw new SyntaxError('node identity is not unique: ' + node.ref);
    unique.add(node.ref);
  }
  return nodes;
}

export function sourceGraphReferences(behavior) {
  return Object.fromEntries(sourceGraphShape(behavior)
    .filter((node) => node.variable !== undefined)
    .map((node) => [node.ref, node.variable]));
}

function mappedGraphReferences(map, fallbackBehavior = null) {
  const mapped = new Set(Object.keys(map.refs ?? {}));
  if (map.node_references) {
    return Object.fromEntries(Object.entries(map.node_references).filter(([ref]) => mapped.has(ref)));
  }
  if (fallbackBehavior) {
    return Object.fromEntries(Object.entries(sourceGraphReferences(fallbackBehavior)).filter(([ref]) => mapped.has(ref)));
  }
  const references = {};
  let states = [];
  try { states = JSON.parse(map.state_signature ?? '[]'); } catch {}
  for (const state of states) references[stateGetRef(state.name)] = state.name;
  const visit = (statements = []) => {
    for (const statement of statements) {
      if (statement.kind === 'set_mutation' && statement.ref && statement.set) {
        references[statement.ref] = statement.set;
        if (statement.key?.startsWith('set:') && statement.key.includes('.add(')) {
          references[setMutationSourceRef(statement.ref)] = statement.set;
        }
      }
      visit(statement.yes);
      visit(statement.no);
      visit(statement.body);
    }
  };
  for (const handler of map.source_tree?.handlers ?? []) visit(handler.body);
  return Object.fromEntries(Object.entries(references).filter(([ref]) => mapped.has(ref)));
}

export function buildOperations(behavior, selectors = null) {
  const refs = new Map(sourceGraphShape(behavior).map((node) => [node.ref, selectors?.[node.ref] ?? node.ref]));
  const operations = [];
  const add = (operation) => operations.push(operation);
  if (!selectors) {
    for (const state of behavior.stateVariables ?? []) {
      const variable = stateVariableRef(state.name);
      add({ op: 'add_variable', ref: variable, name: state.name, type: graphType(state.type),
        ...(state.type === 'entity' ? { type_name: 'ecs.entity' } : {}),
        container: state.container, default: state.default });
      add({ op: 'add_variable_node', ref: stateGetRef(state.name), variable: state.name, kind: 'get' });
    }
  }
  for (const handler of behavior.handlers) {
    add({ op: 'add_node', type: 'Event', event_name: handler.eventSourceName ?? handler.event, ref: refs.get(eventRef(handler.event)) });
  }
  if (refs.has('self')) add({ op: 'add_self_node', ref: refs.get('self') });
  for (const handler of behavior.handlers) {
    for (const statement of flatten(handler.body)) {
      if (statement.kind === 'expression') {
        add({ op: 'add_node', type: 'Behavior Expression', source: statement.resolvedSource ?? statement.source, ref: refs.get(statement.id) });
        addBindings(add, refs.get(statement.id), statement.resolvedSource ?? statement.source, handler.params, statement.componentInputs);
        addExpressionDisplayNames(add, refs.get(statement.id), statement, handler, statement.resolvedSource ?? statement.source);
      }
      if (statement.kind === 'if') {
        if (statement.conditionSet) {
          add({ op: 'add_node', type: 'Set Contains', ref: refs.get(setConditionRef(statement.id)) });
          add({ op: 'add_node', type: 'Branch', ref: refs.get(statement.id) });
        } else {
          add({ op: 'add_node', type: 'Behavior Expression', source: statement.resolvedCondition ?? statement.condition, ref: refs.get(statement.id) });
          addBindings(add, refs.get(statement.id), statement.resolvedCondition ?? statement.condition, handler.params, statement.componentInputs);
          addExpressionDisplayNames(add, refs.get(statement.id), statement, handler, statement.resolvedCondition ?? statement.condition);
        }
      }
      if (statement.kind === 'foreach') add({ op: 'add_node', type: 'ForEach', ref: refs.get(statement.id) });
      if (statement.kind === 'set_mutation') {
        add({ op: 'add_node', type: statement.method === 'add' ? 'Set Add' : 'Set Remove', ref: refs.get(setMutationRef(statement.id)) });
        if (statement.method === 'add') {
          add({ op: 'add_variable_node', ref: refs.get(setMutationSourceRef(statement.id)), variable: statement.set, kind: 'get' });
        }
        add({ op: 'add_variable_node', ref: refs.get(statement.id), variable: statement.set, kind: 'set' });
      }
      if (statement.kind === 'destroy') add({ op: 'add_node', type: 'Destroy Entity', ref: refs.get(statement.id) });
    }
  }
  for (const handler of behavior.handlers) {
    connectSequence(add, refs, handler.body, { node: refs.get(eventRef(handler.event)), pin: 'Then' });
    for (const statement of flatten(handler.body)) {
      const expression = statement.kind === 'expression' ? statement.resolvedSource ?? statement.source
        : statement.kind === 'if' && !statement.conditionSet ? statement.resolvedCondition ?? statement.condition : null;
      for (const internal of statement.eventInputs ?? []) {
        const pin = handler.eventOutputs?.[internal];
        if (!pin) throw new Error('resolved event input has no output pin mapping: ' + internal);
        const target = statement.kind === 'set_mutation'
          ? { ref: setMutationRef(statement.id), pin: 'Item' }
          : statement.kind === 'if' && statement.conditionSet
            ? { ref: setConditionRef(statement.id), pin: 'Item' }
            : { ref: statement.id, pin: internal };
        add({ op: 'connect', from: refs.get(eventRef(handler.event)), from_pin: pin,
          to: refs.get(target.ref), to_pin: target.pin, kind: 'data' });
      }
      if (expression) {
        const symbols = new Set(expressionSymbols(expression));
        for (const loop of statement.loopBindings ?? []) {
          if (!symbols.has(loop.name)) continue;
          add({ op: 'connect', from: refs.get(loop.statement.id), from_pin: 'Array Element',
            to: refs.get(statement.id), to_pin: loop.name, kind: 'data' });
        }
      }
      if (statement.kind === 'set_mutation') {
        const setSource = statement.method === 'add' ? setMutationSourceRef(statement.id) : stateGetRef(statement.set);
        add({ op: 'connect', from: refs.get(setSource), from_pin: 'Value',
          to: refs.get(setMutationRef(statement.id)), to_pin: 'Set', kind: 'data' });
        if (statement.setOperand.kind === 'loop') {
          add({ op: 'connect', from: refs.get(statement.setOperand.statement.id), from_pin: 'Array Element',
            to: refs.get(setMutationRef(statement.id)), to_pin: 'Item', kind: 'data' });
        }
        add({ op: 'connect', from: refs.get(setMutationRef(statement.id)), from_pin: 'Set',
          to: refs.get(statement.id), to_pin: 'Value', kind: 'data' });
      }
      if (statement.kind === 'if' && statement.conditionSet) {
        add({ op: 'connect', from: refs.get(stateGetRef(statement.conditionSet.name)), from_pin: 'Value',
          to: refs.get(setConditionRef(statement.id)), to_pin: 'Set', kind: 'data' });
        if (statement.conditionSet.operand.kind === 'loop') {
          add({ op: 'connect', from: refs.get(statement.conditionSet.operand.statement.id), from_pin: 'Array Element',
            to: refs.get(setConditionRef(statement.id)), to_pin: 'Item', kind: 'data' });
        }
        add({ op: 'connect', from: refs.get(setConditionRef(statement.id)), from_pin: 'Return Value',
          to: refs.get(statement.id), to_pin: 'Condition', kind: 'data' });
      }
    }
  }
  return operations;
}

function addBindings(add, node, expression, params, componentInputs = []) {
  const used = new Set(expressionSymbols(expression));
  for (const input of componentInputs) {
    if (!used.has(input.name)) continue;
    add({ op: 'bind_input', node, pin: input.name, kind: 'component', component: input.componentKey, field: input.field });
  }
  for (const parameter of params) {
    if (!used.has(parameter.name)) continue;
    if (parameter.kind === 'component') {
      add({ op: 'bind_input', node, pin: parameter.name, kind: 'component', component: parameter.componentKey });
    }
  }
}

function addExpressionDisplayNames(add, node, statement, handler, expression) {
  const used = new Set(expressionSymbols(expression));
  for (const input of statement.componentInputs ?? []) {
    if (!used.has(input.name)) continue;
    const displayName = input.alias && input.field ? `${input.alias}.${input.field}` : input.name;
    add({ op: 'set_pin', node, pin: input.name, display_name: displayName });
  }
  for (const internal of statement.eventInputs ?? []) {
    if (!used.has(internal)) continue;
    const payload = handler.eventFields?.[internal];
    if (!payload) continue;
    const sourceName = payload.alias || payload.pin;
    add({ op: 'set_pin', node, pin: internal,
      display_name: sourceName ? `event.${sourceName}` : internal });
  }
}

function connectSequence(add, refs, statements, start) {
  let previous = start;
  for (const statement of statements) {
    const node = refs.get(statement.id);
    add({ op: 'connect', from: previous.node, from_pin: previous.pin, to: node, to_pin: 'Exec', kind: 'exec' });
    if (statement.kind === 'if') {
      connectSequence(add, refs, statement.yes, { node, pin: 'True' });
      connectSequence(add, refs, statement.no, { node, pin: 'False' });
      return;
    }
    if (statement.kind === 'foreach') {
      add({ op: 'connect', from: refs.get(stateGetRef(statement.array)), from_pin: 'Value',
        to: node, to_pin: 'Array', kind: 'data' });
      connectSequence(add, refs, statement.body, { node, pin: 'Body' });
      previous = { node, pin: 'Completed' };
      continue;
    }
    if (statement.kind === 'destroy') {
      add({ op: 'connect', from: refs.get('self'), from_pin: 'Self', to: node, to_pin: 'Entity', kind: 'data' });
    }
    previous = { node, pin: 'Then' };
  }
}

export function eventRef(event) {
  return 'event-' + event.replace(/[^A-Za-z0-9_-]/g, '-');
}

function stateVariableRef(name) { return 'state-variable-' + name; }
function stateGetRef(name) { return 'state-get-' + name; }
export function setMutationRef(id) { return id + '-set-op'; }
function setMutationSourceRef(id) { return id + '-set-source'; }
export function setConditionRef(id) { return id + '-set-test'; }
function graphType(type) { return type === 'string' ? 'const char *' : type === 'entity' ? 'handle' : type; }

export function expectedConnections(behavior, refs) {
  const links = [];
  const add = (from, fromPin, to, toPin, kind) => links.push({
    from: Number(refs[from]), from_pin: fromPin, to: Number(refs[to]), to_pin: toPin, kind,
  });
  const sequence = (statements, start) => {
    let previous = start;
    for (const statement of statements) {
      add(previous.ref, previous.pin, statement.id, 'Exec', 'exec');
      if (statement.kind === 'if') {
        if (statement.conditionSet) {
          add(stateGetRef(statement.conditionSet.name), 'Value', setConditionRef(statement.id), 'Set', 'data');
          if (statement.conditionSet.operand.kind === 'loop') {
            add(statement.conditionSet.operand.statement.id, 'Array Element', setConditionRef(statement.id), 'Item', 'data');
          }
          add(setConditionRef(statement.id), 'Return Value', statement.id, 'Condition', 'data');
        }
        sequence(statement.yes, { ref: statement.id, pin: 'True' });
        sequence(statement.no, { ref: statement.id, pin: 'False' });
        return;
      }
      if (statement.kind === 'foreach') {
        add(stateGetRef(statement.array), 'Value', statement.id, 'Array', 'data');
        sequence(statement.body, { ref: statement.id, pin: 'Body' });
        previous = { ref: statement.id, pin: 'Completed' };
        continue;
      }
      if (statement.kind === 'destroy') add('self', 'Self', statement.id, 'Entity', 'data');
      if (statement.kind === 'set_mutation') {
        const setSource = statement.method === 'add' && refs[setMutationSourceRef(statement.id)] !== undefined
          ? setMutationSourceRef(statement.id)
          : stateGetRef(statement.set);
        add(setSource, 'Value', setMutationRef(statement.id), 'Set', 'data');
        if (statement.setOperand.kind === 'loop') {
          add(statement.setOperand.statement.id, 'Array Element', setMutationRef(statement.id), 'Item', 'data');
        }
        add(setMutationRef(statement.id), 'Set', statement.id, 'Value', 'data');
      }
      previous = { ref: statement.id, pin: 'Then' };
    }
  };
  for (const handler of behavior.handlers) {
    sequence(handler.body, { ref: eventRef(handler.event), pin: 'Then' });
    for (const statement of flatten(handler.body)) {
      for (const internal of statement.eventInputs ?? []) {
        const pin = handler.eventOutputs?.[internal];
        const target = statement.kind === 'set_mutation'
          ? [setMutationRef(statement.id), 'Item']
          : statement.kind === 'if' && statement.conditionSet
            ? [setConditionRef(statement.id), 'Item']
            : [statement.id, internal];
        if (pin) add(eventRef(handler.event), pin, target[0], target[1], 'data');
      }
      const expression = statement.kind === 'expression' ? statement.resolvedSource ?? statement.source
        : statement.kind === 'if' && !statement.conditionSet ? statement.resolvedCondition ?? statement.condition : null;
      if (!expression) continue;
      const symbols = new Set(expressionSymbols(expression));
      for (const loop of statement.loopBindings ?? []) {
        if (symbols.has(loop.name)) add(loop.statement.id, 'Array Element', statement.id, loop.name, 'data');
      }
    }
  }
  return links;
}

export function verifyTopology(graph, behavior, refs) {
  const ownIds = new Set(Object.values(refs).map(Number));
  const expected = expectedConnections(behavior, refs).map(connectionKey).sort();
  const nodes = new Map((graph.nodes ?? []).map((node) => [node.id, node]));
  const actual = (graph.edges ?? []).filter((edge) => {
    const from = endpoint(edge.from).id;
    const to = endpoint(edge.to).id;
    return ownIds.has(from) || ownIds.has(to);
  }).map((edge) => {
    const from = endpoint(edge.from);
    const to = endpoint(edge.to);
    const source = nodes.get(from.id);
    const kind = edge.kind ?? (pinType(source, 'out', from.pin) === 'exec' ? 'exec' : 'data');
    return edgeKey(edge.from, edge.to, kind);
  }).sort();
  if (JSON.stringify(actual) !== JSON.stringify(expected)) {
    throw new Error('source-owned graph wiring changed outside the supported structured subset; inspectGraph and repair the code-owned links before compiling or decompiling');
  }
  for (const descriptor of sourceGraphShape(behavior)) {
    const id = Number(refs[descriptor.ref]);
    const node = nodes.get(id);
    if (!node || node.type !== descriptor.type) throw new Error('source node ' + descriptor.ref + ' is missing or changed type');
    if (descriptor.type === 'Event' && node.reference_name !== descriptor.event) throw new Error('event node ' + descriptor.ref + ' no longer matches its source handler');
    if (descriptor.variable !== undefined && node.reference_name !== descriptor.variable) {
      throw new Error('variable node ' + descriptor.ref + ' no longer matches its source binding');
    }
  }
}

function endpoint(value) {
  const dot = String(value).indexOf('.');
  return { id: Number(String(value).slice(0, dot)), pin: String(value).slice(dot + 1) };
}

function pinType(node, side, name) {
  const entry = (node?.[side] ?? []).find((pin) => pin.slice(0, pin.indexOf(':')).trim() === name);
  return entry?.slice(entry.indexOf(':') + 1).trim();
}

function edgeKey(from, to, kind) {
  return endpoint(from).id + '.' + endpoint(from).pin + '->' + endpoint(to).id + '.' + endpoint(to).pin + ':' + kind;
}

function connectionKey(connection) {
  return connection.from + '.' + connection.from_pin + '->' + connection.to + '.' + connection.to_pin + ':' + connection.kind;
}

export function verifyMappedTopology(graph, map, behavior = null) {
  const ownIds = new Set(Object.values(map.refs).map(Number));
  const nodes = new Map((graph.nodes ?? []).map((node) => [node.id, node]));
  const expectedEdges = map.edges ?? (behavior ? expectedConnections(behavior, map.refs) : null);
  if (!expectedEdges) throw new Error('source map needs its original code shape to verify ownership');
  const expected = expectedEdges.map(connectionKey).sort();
  const actual = (graph.edges ?? []).filter((edge) => {
    return ownIds.has(endpoint(edge.from).id) || ownIds.has(endpoint(edge.to).id);
  }).map((edge) => {
    const from = endpoint(edge.from);
    const source = nodes.get(from.id);
    const kind = edge.kind ?? (pinType(source, 'out', from.pin) === 'exec' ? 'exec' : 'data');
    return edgeKey(edge.from, edge.to, kind);
  }).sort();
  if (JSON.stringify(actual) !== JSON.stringify(expected)) {
    throw new Error('source-owned graph wiring changed outside the supported structured subset; inspectGraph and repair the code-owned links before compiling or decompiling');
  }
  for (const [ref, id] of Object.entries(map.refs)) {
    const node = nodes.get(Number(id));
    if (!node || node.type !== map.types[ref]) throw new Error('source node ' + ref + ' is missing or changed type');
  }
  const references = mappedGraphReferences(map, behavior);
  for (const [ref, variable] of Object.entries(references)) {
    const node = nodes.get(Number(map.refs[ref]));
    if (!node || node.reference_name !== variable) throw new Error('variable node ' + ref + ' no longer matches its source binding');
  }
}

export function updateGraphOperations(behavior, map, graph, fallbackBehavior = null) {
  verifyMappedTopology(graph, map, fallbackBehavior);
  const oldReferences = mappedGraphReferences(map, fallbackBehavior);
  const current = new Map((graph.nodes ?? []).map((node) => [node.id, node]));
  const descriptors = sourceGraphShape(behavior);
  const selectors = {};
  const retained = new Set();
  const added = new Set();
  for (const descriptor of descriptors) {
    const id = Number(map.refs[descriptor.ref]);
    const expectedReference = oldReferences[descriptor.ref];
    if (descriptor.variable !== undefined && Number.isSafeInteger(id) && expectedReference === undefined) {
      throw new Error('source map is missing variable reference metadata for ' + descriptor.ref);
    }
    const referenceMatches = descriptor.variable === undefined || expectedReference === descriptor.variable;
    if (Number.isSafeInteger(id) && map.types[descriptor.ref] === descriptor.type && referenceMatches) {
      selectors[descriptor.ref] = id;
      retained.add(descriptor.ref);
      continue;
    }
    selectors[descriptor.ref] = descriptor.ref;
    added.add(descriptor.ref);
  }

  const oldIds = new Set(Object.values(map.refs).map(Number));
  const operations = [];
  for (const edge of graph.edges ?? []) {
    const from = endpoint(edge.from);
    const to = endpoint(edge.to);
    if (!oldIds.has(from.id) && !oldIds.has(to.id)) continue;
    const source = current.get(from.id);
    const kind = edge.kind ?? (pinType(source, 'out', from.pin) === 'exec' ? 'exec' : 'data');
    operations.push({ op: 'disconnect', from: from.id, from_pin: from.pin, to: to.id, to_pin: to.pin, kind });
  }

  for (const [ref, id] of Object.entries(map.refs)) {
    if (!retained.has(ref)) operations.push({ op: 'remove_node', node: Number(id) });
  }
  for (const handler of behavior.handlers) {
    for (const statement of flatten(handler.body)) {
      if (!retained.has(statement.id)) continue;
      if (statement.kind === 'expression') operations.push({ op: 'set_expression', node: selectors[statement.id], source: statement.resolvedSource ?? statement.source });
      if (statement.kind === 'if' && !statement.conditionSet) operations.push({ op: 'set_expression', node: selectors[statement.id], source: statement.resolvedCondition ?? statement.condition });
    }
  }

  const planned = buildOperations(behavior, selectors);
  for (const operation of planned) {
    if ((operation.op === 'add_node' || operation.op === 'add_self_node' || operation.op === 'add_variable_node') && added.has(operation.ref)) operations.push(operation);
  }
  for (const operation of planned) if (operation.op === 'bind_input') operations.push(operation);
  for (const operation of planned) {
    if (operation.op === 'set_pin' && operation.display_name !== undefined) operations.push(operation);
  }
  for (const operation of planned) if (operation.op === 'connect') operations.push(operation);
  return { operations, selectors, added };
}

export function updateOperations(behavior, refs) {
  const operations = [];
  for (const handler of behavior.handlers) {
    for (const statement of flatten(handler.body)) {
      if (statement.kind === 'expression') {
        operations.push({ op: 'set_expression', node: Number(refs[statement.id]), source: statement.resolvedSource ?? statement.source });
        addBindings((operation) => operations.push({ ...operation, node: Number(refs[statement.id]) }), Number(refs[statement.id]), statement.resolvedSource ?? statement.source, handler.params, statement.componentInputs);
        addExpressionDisplayNames((operation) => operations.push({ ...operation, node: Number(refs[statement.id]) }), Number(refs[statement.id]), statement, handler, statement.resolvedSource ?? statement.source);
      }
      if (statement.kind === 'if' && !statement.conditionSet) {
        operations.push({ op: 'set_expression', node: Number(refs[statement.id]), source: statement.resolvedCondition ?? statement.condition });
        addBindings((operation) => operations.push({ ...operation, node: Number(refs[statement.id]) }), Number(refs[statement.id]), statement.resolvedCondition ?? statement.condition, handler.params, statement.componentInputs);
        addExpressionDisplayNames((operation) => operations.push({ ...operation, node: Number(refs[statement.id]) }), Number(refs[statement.id]), statement, handler, statement.resolvedCondition ?? statement.condition);
      }
    }
  }
  return operations;
}

export function graphToSource(source, behavior, graph, map, registry) {
  if (!registry) throw new Error('graph-to-source export requires the reflected registry for full source validation');
  verifyTopology(graph, behavior, map.refs);
  const nodes = new Map((graph.nodes ?? []).map((node) => [node.id, node]));
  const edits = [];
  for (const handler of behavior.handlers) {
    const expressions = flatten(handler.body).map((statement) => {
      const node = nodes.get(Number(map.refs[statement.id]));
      let graphSource = node?.reference_name;
      for (const [internal, field] of Object.entries(handler.eventFields ?? {})) {
        graphSource = graphSource?.replace(new RegExp('(^|[^A-Za-z0-9_])' + internal + '(?![A-Za-z0-9_])', 'g'), '$1event.' + field.alias);
      }
      for (const [internal, field] of Object.entries(handler.componentFields ?? {})) {
        if (!graphSource || !expressionSymbols(graphSource).includes(internal)) continue;
        const binding = node.inputs?.[internal]?.binding;
        if (!binding) throw new Error('input ' + internal + ' lost its structured component binding');
        if (binding.kind !== 'component_field' || binding.component !== field.componentKey || !binding.field) {
          throw new Error('component field input ' + internal + ' no longer matches its reflected binding');
        }
        graphSource = graphSource.replace(new RegExp('(^|[^A-Za-z0-9_])' + internal + '(?![A-Za-z0-9_])', 'g'), '$1' + field.alias + '.' + binding.field);
      }
      const wiredLoops = new Set();
      for (const loop of statement.loopBindings ?? []) {
        const sourceId = Number(map.refs[loop.statement.id]);
        if (!Number.isSafeInteger(sourceId) || !node) continue;
        const wired = (graph.edges ?? []).some((edge) => {
          const from = endpoint(edge.from);
          const to = endpoint(edge.to);
          const sourceNode = nodes.get(from.id);
          const kind = edge.kind ?? (pinType(sourceNode, 'out', from.pin) === 'exec' ? 'exec' : 'data');
          return from.id === sourceId && from.pin === 'Array Element'
            && to.id === node.id && to.pin === loop.name && kind === 'data';
        });
        if (wired) wiredLoops.add(loop.name);
      }
      const sourceNames = new Set([
        'event', 'pi', 'e',
        ...handler.params.map((parameter) => parameter.name),
        ...wiredLoops,
      ]);
      for (const symbol of expressionSymbols(graphSource ?? '')) {
        if (sourceNames.has(symbol)) continue;
        const input = node.inputs?.[symbol];
        const binding = input?.binding;
        if (!binding) {
          if (input && Object.hasOwn(input, 'default')) {
            const literal = graphInputDefaultSource(input);
            if (literal === null) throw new Error('input ' + symbol + ' has a default outside the supported scalar literal subset');
            graphSource = graphSource.replace(new RegExp('(^|[^A-Za-z0-9_])' + symbol + '(?![A-Za-z0-9_])', 'g'), '$1(' + literal + ')');
            continue;
          }
          throw new Error('input ' + symbol + ' has no verified source binding or supported default');
        }
        if (binding.kind !== 'component_field' || !binding.field) {
          throw new Error('input ' + symbol + ' has a graph binding that the source subset cannot represent');
        }
        const parameter = handler.params.find((candidate) => candidate.componentKey === binding.component);
        if (!parameter) throw new Error('input ' + symbol + ' binds a component field with no declared source alias');
        const field = (registry.components ?? []).find((component) => component.key === binding.component)
          ?.fields?.find((candidate) => candidate.name === binding.field);
        if (!field) throw new Error('input ' + symbol + ' binds an unknown reflected component field');
        graphSource = graphSource.replace(new RegExp('(^|[^A-Za-z0-9_])' + symbol + '(?![A-Za-z0-9_])', 'g'), '$1' + parameter.name + '.' + binding.field);
      }
      return { statement, node, source: graphSource };
    }).filter((entry) => entry.source);
    const inferredOverrides = [];
    for (const parameter of handler.params) {
      if (parameter.kind === 'component') continue;
      const bindings = [];
      for (const entry of expressions) {
        if (!expressionSymbols(entry.source).includes(parameter.name)) continue;
        const binding = entry.node.inputs?.[parameter.name]?.binding;
        if (!binding) throw new Error('input ' + parameter.name + ' lost its structured binding');
        bindings.push(binding);
      }
        if (bindings.length && parameter.kind === 'component') {
          const first = bindings[0];
          if (bindings.some((binding) => JSON.stringify(binding) !== JSON.stringify(first))) {
            throw new Error('component alias ' + parameter.name + ' has different bindings in different expressions');
          }
          if (first.kind !== 'component' || first.component !== parameter.componentKey) {
            throw new Error('component alias ' + parameter.name + ' no longer matches its reflected binding');
          }
        } else if (bindings.length) {
        const normalized = JSON.stringify(bindings[0]);
        if (bindings.some((binding) => JSON.stringify(binding) !== normalized)) {
          throw new Error('input ' + parameter.name + ' has different bindings in different expressions');
        }
        if (Number.isInteger(parameter.start) && Number.isInteger(parameter.end)) {
          edits.push({ start: parameter.start, end: parameter.end, text: selectorText(bindings[0]) });
        } else if (handler.componentContext && !matchesContextBinding(handler.componentContext, parameter, bindings[0])) {
          inferredOverrides.push(parameter.name + ': ' + selectorText(bindings[0]));
        } else if (!handler.componentContext) {
          throw new Error('input ' + parameter.name + ' has no source binding span');
        }
      }
    }
    if (inferredOverrides.length) {
      edits.push({ start: handler.contextClose, end: handler.contextClose, text: ', ' + inferredOverrides.join(', ') });
    }
    for (const statement of flatten(handler.body)) {
      const expression = statement.kind === 'expression' ? statement.source
        : statement.kind === 'if' && !statement.conditionSet ? statement.condition : null;
      if (!expression) continue;
      const node = nodes.get(Number(map.refs[statement.id]));
      if (!node?.reference_name) throw new Error('expression node ' + statement.id + ' has no readable source');
      const expressionEntry = expressions.find((entry) => entry.statement === statement);
      edits.push({ start: statement.span.start, end: statement.span.end, text: expressionEntry?.source ?? node.reference_name });
    }
  }
  edits.sort((a, b) => b.start - a.start);
  let output = source;
  for (const edit of edits) output = output.slice(0, edit.start) + edit.text + output.slice(edit.end);
  resolveBehavior(parseBehavior(output), registry);
  return output;
}

function selectorText(binding) {
  throw new Error('graph binding is outside the supported source subset: ' + binding.kind);
}

function graphInputDefaultSource(input) {
  if (!input || !Object.hasOwn(input, 'default')) return null;
  const value = input.default;
  if (typeof value === 'number' && Number.isFinite(value)) return String(value);
  if (typeof value === 'boolean') return String(value);
  if (typeof value === 'string') return JSON.stringify(value);
  return null;
}

export function readBehaviorFile(path) {
  return parseBehavior(readFileSync(path, 'utf8'));
}
