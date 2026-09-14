import fs from 'fs';
import path from 'path';
import { spawn, exec } from 'child_process';
import blessed from 'blessed';
import { Tail } from 'tail';
import kill from 'kill-port';
import {
    resolveCwdPromptFlag,
    withGnomeTerminalWorkdir,
    resolveSpawnCwd,
    promptForWorkingDirectory,
    normalizeKeySpec,
    formatKeyLabel
} from './dashboard-ext.mjs';

// Added: spawn fallback when script file is missing in scriptsDir
function spawnWithScriptsDirFallback(scriptsDir, scriptFile, spawnImpl) {
    const candidate = require('path').join(scriptsDir ?? './scripts', scriptFile);
    const exists = require('fs').existsSync(candidate);
    if (exists) {
        return { cmd: candidate, usedFallback: false };
    }
    // Fallback: run the provided value as-is via shell
    return { cmd: scriptFile, usedFallback: true };
}
/**
 * dashboard.json format:
 * {
 *   "logs": [ "<path>", ... ],
 *   "scriptsDir": "<relative path to scripts>",
 *   "keyBindings": { "<key>": "<script file>", ... },
 *   "commands": [
 *     "<script file>",
 *     { "label": "<display label>", "script": "<script file>", "key": "<optional key>", "promptCwd": false },
 *     ...
 *   ],
 *   "commandSets": [
 *     { "name": "<set name>", "commands": [
 *       { "label": "<display label>", "script": "<script file>", "key": "<optional key>", "promptCwd": true },
 *       ...
 *     ] }
 *   ],
 *   "settings": { ... },
 *   "events": {
 *     "onDashboardStart": [
 *       { "action": "killProcess" | "closeTCPPorts" | "closeUDPPorts", "parameters": [ ... ] },
 *       ...
 *     ],
 *     "beforeStartProcess": [
 *       {
 *         "script": "<script file name, including extension>",
 *         "actions": [
 *           { "action": "killProcess" | "closeTCPPorts" | "closeUDPPorts", "parameters": [ ... ] },
 *           ...
 *         ]
 *       },
 *       ...
 *     ]
 *   }
 * }
 *
 * Notes:
 * - Script matching uses the full file name (with extension).
 * - Actions available in both places: killProcess, closeTCPPorts, closeUDPPorts.
 * - For backward compatibility, legacy top-level keys `onDashboardStart` and
 *   `beforeStartProcess` (object map) are accepted and normalized into `events`.
 * - Per-command working directory prompt: set `promptCwd: true` (aliases
 *   accepted: `cwdPrompt`, `askCwd`, `cwdRequired`) on a command entry.
 *   When the entry is activated (list selection or key shortcut), a folder
 *   selector modal opens first; the chosen directory is used as `cwd` for
 *   the spawned process. Cancelling aborts the launch.
 * - Key shortcuts use whole words joined with "+": "control+b", "shift+a",
 *   "alt+c", "function+f1", "control+shift+s", "escape", "f5".
 *   Words accepted: control/ctrl, shift, alt/meta, function/fn (F-keys only).
 *   Raw blessed names ("C-b", "S-a", "M-c") still work. Invalid specs bind
 *   nothing (the entry stays selectable from the list).
 */

/*
setTimeout(() => {
    output.log('[dash] Timeout reached, exiting.');
    screen.render();
    process.exit(1);
}, 60000);
*/

const defaultSettings = {
    enableCommandHistory: true,
    persistLogs: true,
    notifyOnExit: true,
    showCustomCommand: true,
    showLogWindow: true,
    renderThrottleMs: 100,
    maxLogLines: 1000,
    logBatchSize: 50,
    maxQueuedLogLines: 2000,
    sidebarMinWidth: 32
};

function parsePositiveInteger(value) {
    if (typeof value === 'number') {
        if (!Number.isFinite(value) || value <= 0) return undefined;
        return Math.floor(value);
    }
    if (typeof value !== 'string' || value.trim() === '') return undefined;
    const parsed = parseInt(value, 10);
    return Number.isFinite(parsed) && parsed > 0 ? parsed : undefined;
}

function normalizeSettings(overrides) {
    const normalized = { ...defaultSettings };
    if (!overrides || typeof overrides !== 'object') {
        return normalized;
    }

    const booleanKeys = ['enableCommandHistory', 'persistLogs', 'notifyOnExit', 'showCustomCommand', 'showLogWindow'];
    for (const key of booleanKeys) {
        if (typeof overrides[key] === 'boolean') {
            normalized[key] = overrides[key];
        }
    }

    const positiveIntegerKeys = ['renderThrottleMs', 'maxLogLines', 'logBatchSize', 'maxQueuedLogLines', 'sidebarMinWidth'];
    for (const key of positiveIntegerKeys) {
        const parsed = parsePositiveInteger(overrides[key]);
        if (parsed !== undefined) {
            if (key === 'sidebarMinWidth') {
                normalized[key] = Math.max(parsed, 32);
                continue;
            }
            normalized[key] = parsed;
        }
    }

    return normalized;
}

let currentSettings = { ...defaultSettings };
const logQueues = new Map();
let screen = null;

let logStream = null;

// interface LastCommand { kind: 'script' | 'custom'; value: string; }
let lastCommand = null;

// Throttle rendering to avoid performance issues with high-frequency log updates
let renderScheduled = false;
function scheduleRender() {
    if (renderScheduled) return;
    renderScheduled = true;
    const delay = currentSettings.renderThrottleMs ?? defaultSettings.renderThrottleMs;
    setTimeout(() => {
        renderScheduled = false;
        if (screen && typeof screen.render === 'function') {
            screen.render();
        }
    }, delay);
}

function ensureLogQueue(box) {
    let entry = logQueues.get(box);
    if (!entry) {
        entry = { pending: [], processing: false, backlogDropped: false };
        logQueues.set(box, entry);
    }
    return entry;
}

function flushLogQueue(box) {
    const entry = logQueues.get(box);
    if (!entry) return;
    const batchSize = currentSettings.logBatchSize ?? defaultSettings.logBatchSize;
    const batch = entry.pending.splice(0, batchSize);
    if (entry.backlogDropped) {
        box.log('... [log backlog trimmed to keep dashboard responsive] ...');
        entry.backlogDropped = false;
    }
    for (const line of batch) {
        box.log(line);
    }
    const maxLines = currentSettings.maxLogLines ?? defaultSettings.maxLogLines;
    if (box.getLines().length > maxLines) {
        box.setContent('... [log cleared to prevent memory overflow] ...\n');
    }
    scheduleRender();
    if (entry.pending.length > 0) {
        setImmediate(() => flushLogQueue(box));
    } else {
        entry.processing = false;
    }
}

function enqueueLogLine(box, line) {
    const entry = ensureLogQueue(box);
    entry.pending.push(line);
    const maxQueued = currentSettings.maxQueuedLogLines ?? defaultSettings.maxQueuedLogLines;
    if (entry.pending.length > maxQueued) {
        entry.pending = entry.pending.slice(entry.pending.length - maxQueued);
        entry.backlogDropped = true;
    }
    if (!entry.processing) {
        entry.processing = true;
        setImmediate(() => flushLogQueue(box));
    }
}

function initLogFile() {
    if (config.settings?.persistLogs !== true || logStream) return;
    const logDir = path.resolve(process.cwd(), 'log');
    if (!fs.existsSync(logDir)) fs.mkdirSync(logDir, { recursive: true });
    const timestamp = new Date().toISOString().replace(/[:]/g, '-');
    const filePath = path.join(logDir, `dash-${timestamp}.log`);
    logStream = fs.createWriteStream(filePath, { flags: 'a' });
}

function stopLogFile() {
    if (!logStream) return;
    logStream.end();
    logStream = null;
}

function loadConfig() {
    try {
        const cfgPath = path.join(process.cwd(), 'dashboard.json');
        const raw = fs.readFileSync(cfgPath, 'utf-8');
        const cfg = JSON.parse(raw);
        // Normalize legacy keys into events
        const legacyODS = cfg.onDashboardStart || [];
        const legacyBSP = cfg.beforeStartProcess || {};
        const inEvents = cfg.events || {};
        // Convert legacy object map { "scriptName": [actions] } -> array of { script, actions }
        const normalizeMapToArray = (m) => {
            if (!m || Array.isArray(m)) return Array.isArray(m) ? m : [];
            if (typeof m !== 'object') return [];
            return Object.entries(m).map(([script, actions]) => ({
                script,
                actions: Array.isArray(actions) ? actions : []
            }));
        };
        // Determine final events
        const events = {
            onDashboardStart: Array.isArray(inEvents.onDashboardStart) ? inEvents.onDashboardStart
                                : Array.isArray(legacyODS) ? legacyODS : [],
            beforeStartProcess: Array.isArray(inEvents.beforeStartProcess)
                                ? inEvents.beforeStartProcess
                                : normalizeMapToArray(legacyBSP)
        };
        const computedSettings = normalizeSettings(cfg.settings);
        currentSettings = computedSettings;
        return {
            ...cfg,
            events,
            settings: computedSettings
        };
    } catch {
        const fallbackSettings = normalizeSettings();
        currentSettings = fallbackSettings;
        return { settings: fallbackSettings, events: { onDashboardStart: [], beforeStartProcess: [] } };
    }
}


function escapeRegex(s) {
    return s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

function parseLogDate(s) {
    const iso = s.replace(/T(\d{2})-(\d{2})-(\d{2})/, 'T$1:$2:$3');
    return new Date(iso).getTime();
}

function resolveLatest(p) {
    if (!p.includes('{{latest}}')) return p;

    const [before, after] = p.split('{{latest}}');
    const dir = path.dirname(before);
    const prefix = path.basename(before);
    const directory = path.resolve(dir || '.');

    try {
        const regex = new RegExp(`^${escapeRegex(prefix)}(\\d{4}-\\d{2}-\\d{2}(?:T\\d{2}-\\d{2}-\\d{2})?)${escapeRegex(after)}$`);
        const entries = fs.readdirSync(directory)
            .map(name => {
                const match = name.match(regex);
                if (!match) return null;
                return { dateStr: match[1], timestamp: parseLogDate(match[1]) };
            })
            .filter((v) => v !== null)
            .sort((a, b) => b.timestamp - a.timestamp);

        if (entries.length === 0) return path.join(dir, `${prefix}latest${after}`);

        return path.join(dir, `${prefix}${entries[0].dateStr}${after}`);
    } catch {
        return path.join(dir, `${prefix}latest${after}`);
    }
}

function resolveBindingLabel(binding) {
    if (typeof binding === 'object' && binding && typeof binding.label === 'string' && binding.label.trim() !== '') {
        return binding.label;
    }
    if (typeof binding === 'object' && binding && typeof binding.script === 'string' && binding.script.trim() !== '') {
        return binding.script;
    }
    if (typeof binding === 'string' && binding.trim() !== '') {
        return binding;
    }
    return '';
}

function resolveBindingScript(binding) {
    if (typeof binding === 'object' && binding && typeof binding.script === 'string') {
        const trimmed = binding.script.trim();
        return trimmed === '' ? '' : trimmed;
    }
    if (typeof binding === 'string') {
        const trimmed = binding.trim();
        return trimmed === '' ? '' : trimmed;
    }
    return '';
}

function normalizeSidebarEntries() {
    const entries = [];

    for (const [bindingKey, binding] of Object.entries(config.keyBindings ?? {})) {
        const displayLabel = resolveBindingLabel(binding);
        const script = resolveBindingScript(binding);
        const rawKey = String(bindingKey || '').trim();
        const keyName = normalizeKeySpec(rawKey);
        const keyText = formatKeyLabel(rawKey);
        const content = displayLabel !== '' ? displayLabel : rawKey;
        entries.push({
            text: keyText !== '' ? `${keyText} - ${content}` : content,
            label: displayLabel !== '' ? displayLabel : rawKey,
            script,
            keyName,
            selectable: script !== '',
            promptCwd: resolveCwdPromptFlag(binding)
        });
    }

    for (const command of (Array.isArray(config.commands) ? config.commands : [])) {
        const displayLabel = resolveBindingLabel(command);
        const script = resolveBindingScript(command);
        const rawKey = (typeof command === 'object' && command && typeof command.key === 'string')
            ? command.key.trim()
            : '';
        const keyName = normalizeKeySpec(rawKey);
        const keyText = formatKeyLabel(rawKey);
        const fallbackLabel = script !== '' ? script : '(no command)';
        entries.push({
            text: keyText !== '' ? `${keyText} - ${displayLabel || fallbackLabel}` : `${displayLabel || fallbackLabel}`,
            label: displayLabel || fallbackLabel,
            script,
            keyName,
            selectable: script !== '',
            promptCwd: resolveCwdPromptFlag(command)
        });
    }

    return entries;
}

function loadScripts(sidebarEntries) {
    try {
        return sidebarEntries.map((entry) => entry.text);
    } catch {
        return [];
    }
}

function setOutputLabel(label) {
    output.setLabel(` ${label} `);
    screen.render(); // Keep immediate render for UI state changes
}

// interface LogWatcher { configPath: string; currentPath: string; box: blessed.Widgets.Log; tail: Tail; }
function startTail(file, box) {
    if (!fs.existsSync(file)) fs.writeFileSync(file, '');
    const entry = ensureLogQueue(box);
    entry.pending = [];
    entry.processing = false;
    entry.backlogDropped = false;
    const tail = new Tail(file, {
        follow: true,
        useWatchFile: true,
        fsWatchOptions: { interval: 500 }
    });
    tail.on('line', (line) => {
        enqueueLogLine(box, line);
    });
    tail.on('error', (err) => {
        enqueueLogLine(box, `Tail error: ${err.message}`);
    });
    return tail;
}

const config = loadConfig();

let currentCommandSetIndex = 0;

function getCurrentCommandSet() {
    const sets = Array.isArray(config.commandSets) ? config.commandSets : [];
    if (sets.length === 0) return { name: 'empty', commands: [] };
    return sets[currentCommandSetIndex] ?? sets[0];
}

function normalizeSidebarEntriesFromSet(commandSet) {
    const entries = [];

    for (const command of (Array.isArray(commandSet.commands) ? commandSet.commands : [])) {
        if (typeof command === 'object' && command && command.disabled === true) {
            continue;
        }
        const displayLabel = resolveBindingLabel(command);
        const script = resolveBindingScript(command);

        const rawKey = (typeof command === 'object' && command && typeof command.key === 'string')
            ? command.key.trim()
            : '';

        const keyName = normalizeKeySpec(rawKey);
        const keyText = formatKeyLabel(rawKey);
        const fallbackLabel = script !== '' ? script : '(no command)';

        entries.push({
            text: keyText !== '' ? `${keyText} - ${displayLabel || fallbackLabel}` : `${displayLabel || fallbackLabel}`,
            label: displayLabel || fallbackLabel,
            script,
            keyName,
            selectable: script !== '',
            promptCwd: resolveCwdPromptFlag(command)
        });
    }

    return entries;
}

function switchCommandSet(direction = 1) {
    const sets = Array.isArray(config.commandSets) ? config.commandSets : [];
    if (sets.length <= 1) return;

    currentCommandSetIndex += direction;

    if (currentCommandSetIndex < 0) {
        currentCommandSetIndex = sets.length - 1;
    } else if (currentCommandSetIndex >= sets.length) {
        currentCommandSetIndex = 0;
    }

    const currentSet = getCurrentCommandSet();

    sidebarEntries = normalizeSidebarEntriesFromSet(currentSet);
    scripts = loadScripts(sidebarEntries);

    list.setItems(scripts);
    list.select(0);

    sidebar.setLabel(` Scripts (${currentSet.name}) `);

    screen.render();
}

let sidebarEntries = normalizeSidebarEntriesFromSet(getCurrentCommandSet());
let scripts = loadScripts(sidebarEntries);

const dashboardTheme = {
    background: '#1a2f4d',
    panel: '#22436b',
    panelAlt: '#1f3a5d',
    text: '#e3edf9',
    border: '#7ea2cd',
    focus: '#c9ddf3',
    selected: '#4f7fb2',
    hover: '#3f6c9d'
};

// Initialize blessed screen
screen = blessed.screen({
    smartCSR: true,
    title: 'Dashboard',
    style: {
        bg: dashboardTheme.background,
        fg: dashboardTheme.text
    }
});

const SIDEBAR_WIDTH_RATIO = 0.2;

const sidebar = blessed.box({
    top: 0,
    left: 0,
    width: currentSettings.sidebarMinWidth ?? defaultSettings.sidebarMinWidth,
    height: '100%',
    label: 'Scripts',
    border: 'line',
    style: {
        bg: dashboardTheme.panel,
        fg: dashboardTheme.text,
        border: { fg: dashboardTheme.border }
    }
});
const rightContainer = blessed.box({
    top: 0,
    left: currentSettings.sidebarMinWidth ?? defaultSettings.sidebarMinWidth,
    width: '100%',
    height: '100%',
    style: {
        bg: dashboardTheme.panelAlt,
        fg: dashboardTheme.text
    }
});

const logsContainer = blessed.box({
    parent: rightContainer,
    top: 0,
    height: '60%',
    width: '100%',
    label: 'Logs',
    border: 'line',
    style: {
        bg: dashboardTheme.panelAlt,
        fg: dashboardTheme.text,
        border: { fg: dashboardTheme.border }
    }
});

const bottomContainer = blessed.box({
    parent: rightContainer,
    top: '60%',
    height: '40%',
    width: '100%',
    style: {
        bg: dashboardTheme.panelAlt,
        fg: dashboardTheme.text
    }
});

const buttonBar = blessed.box({
    parent: bottomContainer,
    top: 0,
    height: 3,
    width: '100%',
    style: {
        bg: dashboardTheme.panel,
        fg: dashboardTheme.text
    }
});

const STOP_BUTTON_WIDTH = 12;
const controlsContainer = blessed.box({
    parent: buttonBar,
    top: 0,
    left: STOP_BUTTON_WIDTH + 3,
    right: 1,
    height: 3
});

const stopButton = blessed.button({
    parent: buttonBar,
    left: 1,
    mouse: true,
    keys: true,
    shrink: false,
    width: STOP_BUTTON_WIDTH,
    padding: { left: 1, right: 1 },
    content: 'Stop',
    border: 'line',
    style: {
        bg: dashboardTheme.panel,
        fg: dashboardTheme.text,
        border: { fg: dashboardTheme.border },
        focus: {
            bg: dashboardTheme.focus,
            fg: 'black',
            bold: true
        }
    }
});

const inputBox = blessed.textbox({
    parent: controlsContainer,
    left: 0,
    width: '50%-1',
    height: 3,
    inputOnFocus: true,
    border: 'line',
    label: ' Custom Command ',
    style: {
        bg: dashboardTheme.panel,
        fg: dashboardTheme.text,
        border: { fg: dashboardTheme.border },
        focus: {
            border: { fg: dashboardTheme.focus }
        }
    }
});

const filterBox = blessed.textbox({
    parent: controlsContainer,
    left: '50%+1',
    right: 0,
    height: 3,
    inputOnFocus: true,
    border: 'line',
    label: ' Output Filter ',
    style: {
        bg: dashboardTheme.panel,
        fg: dashboardTheme.text,
        border: { fg: dashboardTheme.border },
        focus: {
            border: { fg: dashboardTheme.focus }
        }
    }
});

const output = blessed.log({
    parent: bottomContainer,
    top: 3,
    height: '100%-3',
    width: '100%-3',
    label: 'Process output',
    border: 'line',
    scrollable: true,
    alwaysScroll: true,
    scrollbar: {
        ch: ' ',
        track: { bg: 'grey' },
        style: { inverse: true }
    },
    style: {
        bg: dashboardTheme.panel,
        fg: dashboardTheme.text,
        border: { fg: dashboardTheme.border },
        focus: {
            border: { fg: dashboardTheme.focus }
        }
    }
});

const outputHistory = [];
const outputHistoryLimit = currentSettings.maxQueuedLogLines ?? defaultSettings.maxQueuedLogLines;
let outputFilterValue = '';
const OUTPUT_TRIM_MESSAGE = '... [log cleared to prevent memory overflow] ...';

function storeOutputLine(line, writeToFile = true) {
    outputHistory.push(line);
    while (outputHistory.length > outputHistoryLimit) {
        outputHistory.shift();
    }
    if (writeToFile && logStream) {
        logStream.write(`[${new Date().toISOString()}] ${line}\n`);
    }
}

function refreshOutputDisplay() {
    const filter = outputFilterValue;
    const filtered = filter
        ? outputHistory.filter(line => line.includes(filter))
        : outputHistory.slice();

    const maxLines = currentSettings.maxLogLines ?? defaultSettings.maxLogLines;
    let linesToShow;
    if (!filter && filtered.length > maxLines) {
        linesToShow = filtered.slice(filtered.length - maxLines);
        if (linesToShow.length > 0) {
            linesToShow[0] = OUTPUT_TRIM_MESSAGE;
        }
    } else if (filtered.length > maxLines) {
        linesToShow = filtered.slice(filtered.length - maxLines);
    } else {
        linesToShow = filtered;
    }

    output.setContent(linesToShow.join('\n'));
    if (typeof output.scrollTo === 'function') {
        output.scrollTo(linesToShow.length);
    }
    scheduleRender();
}

function appendOutputLines(message) {
    const normalized = String(message ?? '').replace(/\r/g, '');
    if (normalized === '') {
        storeOutputLine('');
    } else {
        const parts = normalized.split('\n');
        const hasTrailingNewline = normalized.endsWith('\n');
        for (let i = 0; i < parts.length; i++) {
            if (i === parts.length - 1 && parts[i] === '' && !hasTrailingNewline) {
                continue;
            }
            storeOutputLine(parts[i]);
        }
    }
    refreshOutputDisplay();
}

function setOutputFilter(value) {
    const next = value ?? '';
    if (next === outputFilterValue) {
        refreshOutputDisplay();
        return;
    }
    outputFilterValue = next;
    refreshOutputDisplay();
}

output.log = (msg) => {
    appendOutputLines(msg);
};

initLogFile();

const list = blessed.list({
    parent: sidebar,
    width: '100%-3',
    height: '100%-3',
    top: 1,
    left: 1,
    keys: true,
    mouse: true,
    items: scripts,
    style: {
        bg: dashboardTheme.panel,
        fg: dashboardTheme.text,
        border: { fg: dashboardTheme.border },
        selected: { bg: dashboardTheme.selected, fg: dashboardTheme.text },
        item: { hover: { bg: dashboardTheme.hover, fg: dashboardTheme.text } }
    }
});

list.key(['C-right'], () => {
    switchCommandSet(1);
});

list.key(['C-left'], () => {
    switchCommandSet(-1);
});

screen.append(sidebar);
screen.append(rightContainer);

function applyControlsLayout() {
    if (currentSettings.showCustomCommand === false) {
        inputBox.hide();
        filterBox.left = 0;
        filterBox.width = '100%';
        filterBox.right = undefined;
    } else {
        inputBox.show();
        inputBox.left = 0;
        inputBox.width = '50%-1';
        filterBox.left = '50%+1';
        filterBox.width = undefined;
        filterBox.right = 0;
    }
    scheduleRender();
}

function applyRightLayout() {
    if (currentSettings.showLogWindow === false) {
        logsContainer.hide();
        bottomContainer.top = 0;
        bottomContainer.height = '100%';
    } else {
        logsContainer.show();
        logsContainer.top = 0;
        logsContainer.height = '60%';
        bottomContainer.top = '60%';
        bottomContainer.height = '40%';
    }
    scheduleRender();
}

function applySidebarLayout() {
    const minWidth = currentSettings.sidebarMinWidth ?? defaultSettings.sidebarMinWidth;
    const ratioWidth = Math.floor(screen.width * SIDEBAR_WIDTH_RATIO);
    const computedWidth = Math.min(screen.width, Math.max(minWidth, ratioWidth));
    sidebar.width = computedWidth;
    rightContainer.left = computedWidth;
    rightContainer.width = Math.max(screen.width - computedWidth, 0);
    scheduleRender();
}

applySidebarLayout();
applyControlsLayout();
applyRightLayout();
screen.on('resize', applySidebarLayout);

const logBoxes = [];
const logWatchers = [];
(config.logs ?? []).forEach((cfgPath, idx) => {
    const width = Math.floor(100 / ((config.logs ?? []).length || 1));
    const resolved = resolveLatest(cfgPath);
    const box = blessed.log({
        parent: logsContainer,
        label: path.basename(resolved),
        left: `${idx * width}%`,
        width: `${width}%-2`,
        height: '100%-3',
        top: 1,
        border: 'line',
        scrollable: true,
        alwaysScroll: true,
        scrollbar: {
            ch: ' ',
            track: { bg: 'grey' },
            style: { inverse: true }
        },
        style: {
            bg: dashboardTheme.panel,
            fg: dashboardTheme.text,
            border: { fg: dashboardTheme.border },
            focus: {
                border: { fg: dashboardTheme.focus }
            }
        }
    });
    logBoxes.push(box);
    try {
        const tail = startTail(resolved, box);
        logWatchers.push({ configPath: cfgPath, currentPath: resolved, box, tail });
    } catch (err) {
        box.log(`Cannot watch ${resolved}: ${err.message}`);
    }
});

setInterval(() => {
    logWatchers.forEach(w => {
        if (!w.configPath.includes('{{latest}}')) return;
        const newPath = resolveLatest(w.configPath);
        if (newPath === w.currentPath) return;
        try {
            w.tail.unwatch();
        } catch { /* ignore */ }
        w.box.setLabel(path.basename(newPath));
        w.tail = startTail(newPath, w.box);
        w.currentPath = newPath;
        scheduleRender();
    });
}, 60000);

list.focus();

const focusables = [
    list,
    ...logBoxes,
    stopButton,
    ...(currentSettings.showCustomCommand === false ? [] : [inputBox]),
    filterBox,
    output
];
let focusIndex = 0;

screen.key('tab', () => {
    if (typeof cwdSelectorOpen !== 'undefined' && cwdSelectorOpen) return;
    focusIndex = (focusIndex + 1) % focusables.length;
    focusables[focusIndex].focus();
    screen.render(); // Keep immediate render for UI interactions
});

// ---------------------------------------------------------------------------
// Working directory selector (folder browser modal, see ./dashboard-ext.mjs)
// Activated per command via `promptCwd: true` in dashboard.json
// (aliases: `cwdPrompt`, `askCwd`, `cwdRequired`).
// ---------------------------------------------------------------------------

let cwdSelectorOpen = false;

async function launchSidebarEntry(entry) {
    if (!entry || !entry.selectable || entry.script === '') return;
    if (entry.promptCwd === true && !cwdSelectorOpen) {
        cwdSelectorOpen = true;
        try {
            const chosen = await promptForWorkingDirectory(screen, {
                title: entry.label || entry.script,
                initialDir: process.cwd(),
                theme: dashboardTheme,
                scheduleRender,
                fallbackFocus: list
            });
            if (!chosen) {
                output.log('[dash] Working directory selection cancelled.');
                scheduleRender();
                return;
            }
            await runScriptFile(entry.script, { cwd: chosen });
        } finally {
            cwdSelectorOpen = false;
        }
        return;
    }
    await runScriptFile(entry.script);
}

const registeredKeys = new Set();
for (const entry of sidebarEntries) {
    if (!entry.selectable || entry.keyName === '' || registeredKeys.has(entry.keyName)) {
        continue;
    }
    registeredKeys.add(entry.keyName);
    screen.key(entry.keyName, () => {
        launchSidebarEntry(entry).catch((err) => {
            output.log(`Error: ${err?.message || err}`);
            scheduleRender();
        });
    });
}

screen.key(['q', 'C-c'], () => {
    if (cwdSelectorOpen) return; // modal handles its own keys (Escape = cancel)
    if (current) {
        current.kill();
    }
    screen.render(); // Keep immediate render for exit
    process.exit(0);
});

screen.key('C-r', () => {
    if (cwdSelectorOpen) return;
    if (!lastCommand) {
        screen.render(); // Keep immediate render for UI feedback
        return;
    }

    if (lastCommand.kind === 'script') {
        runScript(lastCommand.value, { cwd: lastCommand.cwd }).catch(err => {
            output.log(`Error: ${err.message}`);
            scheduleRender();
        });
    } else {
        runCustomCommand(lastCommand.value, { cwd: lastCommand.cwd });
    }
});

// Keyboard shortcut to stop the current process
screen.key('C-s', () => {
    if (cwdSelectorOpen) return;
    stopCurrentProcess();
});

focusables.forEach(el => {
    el.key('up', () => { el.scroll(-1); screen.render(); }); // Keep immediate render for scrolling
    el.key('down', () => { el.scroll(1); screen.render(); }); // Keep immediate render for scrolling
});

list.on('select', (_, index) => {
    const entry = sidebarEntries[index];
    if (entry?.selectable && entry.script !== '') {
        launchSidebarEntry(entry).catch((err) => {
            output.log(`Error: ${err?.message || err}`);
            scheduleRender();
        });
    }
});

stopButton.on('press', () => {
    stopCurrentProcess();
});

inputBox.on('submit', (value) => {
    const trimmed = (value || '').trim();
    if (!trimmed) return;

    // add to command history
    commandHistory.push(trimmed);
    historyIndex = commandHistory.length;
    runCustomCommand(trimmed);
    inputBox.clearValue();
    screen.render(); // Keep immediate render for UI interactions
});

inputBox.on('keypress', (_, key) => {
    if (config.settings?.enableCommandHistory !== true) return;
    if (commandHistory.length === 0) return;

    if (key.name === 'up') {
        if (historyIndex > 0) historyIndex--;
        inputBox.setValue(commandHistory[historyIndex] ?? '');
        inputBox.cursor = inputBox.getValue().length;
        scheduleRender();
    }

    if (key.name === 'down') {
        if (historyIndex < commandHistory.length - 1) {
            historyIndex++;
            inputBox.setValue(commandHistory[historyIndex] ?? '');
        } else {
            historyIndex = commandHistory.length;
            inputBox.clearValue();
        }
        inputBox.cursor = inputBox.getValue().length;
        scheduleRender();
    }
});

inputBox.on('cancel', () => {
    inputBox.clearValue();
    focusIndex = (focusIndex + 1) % focusables.length;
    focusables[focusIndex].focus();
    screen.render(); // Keep immediate render for UI interactions
});

inputBox.key(['escape'], () => {
    inputBox.cancel();
});

filterBox.on('submit', (value) => {
    setOutputFilter(value ?? '');
    screen.render(); // Keep immediate render for UI interactions
});

filterBox.on('keypress', (_, key) => {
    if (key.name === 'escape') {
        filterBox.cancel();
    }
});

filterBox.on('cancel', () => {
    filterBox.setValue(outputFilterValue);
    focusIndex = (focusIndex + 1) % focusables.length;
    focusables[focusIndex].focus();
    screen.render(); // Keep immediate render for UI interactions
});

// NOTE: moved here exactly like your original file layout
let current = null;
const commandHistory = [];
let historyIndex = -1;

function stopCurrentProcess() {
    if (current) {
        current.kill();
        current = null;
        output.log('Current process killed.');
        scheduleRender();
    }
}

function runCustomCommand(command, options = {}) {
    const trimmed = (command || '').trim();
    if (!trimmed) return;

    if (current) {
        current.kill();
        output.log('Previous process killed.');
    }

    const spawnCwd = resolveSpawnCwd(options.cwd);
    if (spawnCwd) {
        output.log(`[dash] cwd: ${spawnCwd}`);
    }

    const args = trimmed.split(' ');
    const cmd = args.shift();

    current = spawn(cmd, args, { shell: true, ...(spawnCwd ? { cwd: spawnCwd } : {}) });
    setOutputLabel(trimmed);
    lastCommand = { kind: 'custom', value: trimmed, cwd: spawnCwd };

    current.stdout.on('data', data => {
        output.log(data.toString());
        scheduleRender();
    });

    current.stderr.on('data', data => {
        output.log(data.toString());
        scheduleRender();
    });

    current.on('close', code => {
        output.log(`Process exited with code ${code}`);
        current = null;

        if (config.settings?.notifyOnExit) {
            screen.program.bell();
        }

        scheduleRender();
    });
}

async function closePorts(method, ports) {
    for (const p of ports) {
        const port = parseInt(p, 10);
        if (isNaN(port)) continue;
        try {
            await kill(port, method);
            output.log(`Closed ${method.toUpperCase()} port ${port}`);
        } catch (err) {
            output.log(`Error closing ${method.toUpperCase()} port ${port}: ${err.message}`);
        }
        scheduleRender();
    }
}

function execShell(cmd) {
    return new Promise((resolve) => {
        exec(cmd, { windowsHide: true }, (err, stdout, stderr) => {
            resolve({
                code: err ? (err.code || 1) : 0,
                stdout: stdout || '',
                stderr: stderr || (err ? err.message : '')
            });
        });
    });
}

function sleep(ms) { return new Promise(r => setTimeout(r, ms)); }

function isAlive(pid) {
    try { process.kill(pid, 0); return true; }
    catch (e) { return e && e.code === 'EPERM' ? true : false; }
}

async function waitForExit(pid, timeoutMs) {
    const start = Date.now();
    while (Date.now() - start < timeoutMs) {
        if (!isAlive(pid)) return true;
        await sleep(100);
    }
    return !isAlive(pid);
}

// Linux: read /proc/*/cmdline (robust, no ps/pgrep)
// Windows: tasklist CSV
function readCmdline(pid) {
    try {
        const raw = fs.readFileSync(`/proc/${pid}/cmdline`);
        // cmdline is \0-separated
        return raw.toString('utf8').split('\0').filter(Boolean).join(' ');
    } catch {
        return '';
    }
}

async function getPidsByName(name) {
    const needle = String(name).toLowerCase();
    const selfPid = process.pid;
    const results = new Set();

    if (process.platform === 'win32') {
        const { stdout } = await execShell(`tasklist /FO CSV /NH`);
        stdout.split(/\r?\n/).forEach(line => {
            if (!line.trim()) return;
            const parts = line.split('","').map(s => s.replace(/(^"|"$)/g, ''));
            const procName = (parts[0] || '').toLowerCase();
            const pid = parseInt(parts[1], 10);
            if (!isNaN(pid) && pid !== selfPid && procName.includes(needle)) {
                results.add(pid);
            }
        });
        return Array.from(results);
    }

    // Linux /proc scan
    let pids;
    try {
        pids = fs.readdirSync('/proc').filter(d => /^\d+$/.test(d));
    } catch {
        pids = [];
    }

    for (const d of pids) {
        const pid = parseInt(d, 10);
        if (pid === selfPid) continue;
        const cmd = readCmdline(pid).toLowerCase();
        // If cmdline is empty, some daemons: could fall back to exe
        if (!cmd) continue;
        if (cmd.includes(needle)) results.add(pid);
    }
    return Array.from(results);
}

async function killPid(pid) {
    // SIGTERM
    try { process.kill(pid, 'SIGTERM'); } catch (e) {
        if (e && e.code === 'ESRCH') return { ok: true, method: 'already-dead' };
        if (e && e.code === 'EPERM') return { ok: false, method: 'SIGTERM', error: 'EPERM' };
        // other error -> force kill anyway
    }
    const graceful = await waitForExit(pid, 1500);
    if (graceful) return { ok: true, method: 'SIGTERM' };

    // FORCE
    if (process.platform === 'win32') {
        const { code, stderr } = await execShell(`taskkill /PID ${pid} /T /F`);
        return { ok: code === 0, method: 'taskkill', error: code !== 0 ? stderr : undefined };
    } else {
        try { process.kill(pid, 'SIGKILL'); }
        catch (e) {
            if (e && e.code === 'ESRCH') return { ok: true, method: 'SIGKILL' };
            if (e && e.code === 'EPERM') return { ok: false, method: 'SIGKILL', error: 'EPERM' };
            return { ok: false, method: 'SIGKILL', error: e.message || 'unknown' };
        }
        const hard = await waitForExit(pid, 1000);
        return { ok: hard, method: 'SIGKILL', error: hard ? undefined : 'still alive' };
    }
}

async function killProcesses(params) {
    const selfPid = process.pid;

    for (const target of params) {
        // Direct PID
        if (/^\d+$/.test(String(target))) {
            const pid = parseInt(String(target), 10);
            if (pid === selfPid) { continue; }
            const res = await killPid(pid);
            if (res.ok) output.log(`Killed PID ${pid} via ${res.method}`);
            else output.log(`Failed to kill PID ${pid} via ${res.method}: ${res.error || 'unknown'}`);
            scheduleRender();
            continue;
        }

        // Name / substring pattern (cmdline)
        const name = String(target).trim();
        const pids = await getPidsByName(name);

        for (const pid of pids) {
            if (pid === selfPid) { continue; }
            const res = await killPid(pid);
            if (res.ok) output.log(`Killed "${name}" (PID ${pid}) via ${res.method}`);
            else output.log(`Failed to kill "${name}" (PID ${pid}) via ${res.method}: ${res.error || 'unknown'}`);
        }
        scheduleRender();
    }
}


// Execute a list of actions with shared handlers
async function executeActions(actions, label = 'actions') {
    // Normalize and no-op
    if (!actions || actions.length === 0) return;

    for (const act of actions) {
        const params = Array.isArray(act.parameters)
            ? act.parameters
            : (act.parameters != null ? [act.parameters] : []);

        if (act.action === 'closeTCPPorts') {
            output.log(`Closing TCP ports ${params}`);
            await closePorts('tcp', params);
        } else if (act.action === 'closeUDPPorts') {
            output.log(`Closing UDP ports ${params}`);
            await closePorts('udp', params);
        } else if (act.action === 'killProcess') {
            output.log(`Killing processes: ${params}`);
            await killProcesses(params);
        } else {
            output.log(`[${label}] unknown action "${act.action}"`);
        }
    }
    scheduleRender();
}


async function executeBeforeActions(name) {
    const entries = (config.events?.beforeStartProcess || []).filter(e => e && e.script === name);
    const actions = entries.flatMap(e => Array.isArray(e.actions) ? e.actions : []);
    await executeActions(actions, 'pre-launch actions');
}
// Execute actions once when the dashboard starts
async function executeStartupActions() {
    const actions = config.events?.onDashboardStart;
    await executeActions(actions, 'startup actions');
}
async function runScript(name, options = {}) {
    if (current) {
        current.kill();
        current = null;
    }

    output.log(`--------------------------------------------------`);

    await executeBeforeActions(name);
    const spawnCwd = resolveSpawnCwd(options.cwd);
    if (spawnCwd) {
        output.log(`[dash] cwd: ${spawnCwd}`);
    }
    current = spawn('npm', ['run', name], { shell: true, ...(spawnCwd ? { cwd: spawnCwd } : {}) });
    lastCommand = { kind: 'script', value: name, cwd: spawnCwd };

    setOutputLabel(name);

    current.stdout.on('data', data => {
        output.log(data.toString());
        scheduleRender();
    });

    current.stderr.on('data', data => {
        output.log(data.toString());
        scheduleRender();
    });

    current.on('close', code => {
        output.log(`Process exited with code ${code}`);
        current = null;
        scheduleRender();
    });
}

async function runScriptFile(scriptFile, options = {}) {
    if (current) {
        current.kill();
        current = null;
    }

    const scriptPath = path.join(config.scriptsDir ?? './scripts', scriptFile);

    output.log(`--------------------------------------------------`);
    output.log(`[dash] Running: ${scriptPath}`);

    const spawnCwd = resolveSpawnCwd(options.cwd);
    if (spawnCwd) {
        output.log(`[dash] cwd: ${spawnCwd}`);
    }

    await executeBeforeActions(path.basename(scriptFile, path.extname(scriptFile)));

    const spawnOptions = { shell: true, ...(spawnCwd ? { cwd: spawnCwd } : {}) };
    // If the script file doesn't exist in scriptsDir, treat it as a shell command
    if (!fs.existsSync(scriptPath)) {
        const effectiveCommand = withGnomeTerminalWorkdir(scriptFile, spawnCwd);
        current = spawn(effectiveCommand, spawnOptions);
        lastCommand = { kind: 'custom', value: effectiveCommand, cwd: spawnCwd };
        setOutputLabel(scriptFile);
    } else {
        current = spawn(scriptPath, spawnOptions);
        lastCommand = { kind: 'custom', value: scriptPath, cwd: spawnCwd };
        setOutputLabel(path.basename(scriptPath));
    }

    current.stdout.on('data', data => {
        output.log(data.toString());
        scheduleRender();
    });

    current.stderr.on('data', data => {
        output.log(data.toString());
        scheduleRender();
    });

    current.on('close', code => {
        output.log(`Process exited with code ${code}`);
        current = null;
        scheduleRender();
    });
}

// Kick off one-shot startup actions
(async () => {
    try {
        await executeStartupActions();
    } catch (err) {
        output.log(`[startup] error: ${err?.message || err}`);
        scheduleRender();
    }
})();

screen.render();

export { initLogFile, stopLogFile };
