import fs from 'fs';
import path from 'path';
import blessed from 'blessed';

// dashboard-ext.mjs: dashboard helpers merged into a single module
// to keep a single .mjs dependency next to dashboard.mjs.
// Section 1: key specs (was key-spec.mjs).
// Section 2: working directory selector (was cwd-selector.mjs).

// --- Section 1: key specs ---
// Human-friendly key specs for dashboard.json ("control+b", "shift+a",
// "alt+c", "function+f1") translated to the key names blessed emits
// ("C-b", "S-a", "M-c", "f1").
//
// Blessed builds key names as C- (ctrl) plus M- (meta/alt) plus S- (shift)
// plus base (see blessed lib/program.js, lib/keys.js).
//
// "function"/"fn" only makes sense with an F-key ("function+f1" means F1):
// the Fn key itself sends nothing to the terminal, so it cannot be used
// as a real modifier. On most keyboards Fn+F1 simply produces F1 (or a
// media key, depending on Fn-lock): binding "f1" covers the F1 case.

const BASE_ALIASES = {
    return: 'enter',
    esc: 'escape',
    del: 'delete',
    ins: 'insert',
    pgup: 'pageup',
    'page-up': 'pageup',
    pgdown: 'pagedown',
    'page-down': 'pagedown'
};

const KNOWN_BASE = /^(?:[a-z0-9]|space|enter|escape|tab|backspace|delete|insert|home|end|pageup|pagedown|up|down|left|right|clear|f(?:[1-9]|1[0-2]))$/;

export function normalizeKeySpec(spec) {
    if (typeof spec !== 'string') return '';
    const raw = spec.trim();
    if (raw === '') return '';

    // A lone uppercase letter means Shift+letter: terminals send "A"
    // with shift held, which blessed reports as "S-a".
    if (/^[A-Z]$/.test(raw)) return 'S-' + raw.toLowerCase();

    let parts = raw.split('+').map(function (p) { return p.trim().toLowerCase(); }).filter(function (p) { return p !== ''; });

    // Backward compatibility: raw blessed shorthand ("C-b", "M-c", "S-a").
    if (parts.length === 1) {
        const m = parts[0].match(/^((?:[cms]-)+)(.+)$/);
        if (m) {
            const mods = m[1].split('-').filter(Boolean).map(function (c) {
                return c === 'c' ? 'control' : c === 'm' ? 'alt' : 'shift';
            });
            parts = mods.concat([m[2]]);
        }
    }

    let ctrl = false;
    let meta = false;
    let shift = false;
    let fnMarker = false;
    let base = null;

    for (const p of parts) {
        if (p === 'control' || p === 'ctrl') ctrl = true;
        else if (p === 'shift') shift = true;
        else if (p === 'alt' || p === 'meta') meta = true;
        else if (p === 'function' || p === 'fn') fnMarker = true;
        else if (base === null) base = p;
        else return '';
    }

    if (base === null) return '';
    base = BASE_ALIASES[base] || base;

    // "function" only makes sense with an F-key: "function+f1" is F1.
    if (fnMarker && !/^f(?:[1-9]|1[0-2])$/.test(base)) return '';

    if (!KNOWN_BASE.test(base)) {
        // Tolerate single punctuation characters blessed may emit.
        if (base.length !== 1) return '';
    }

    // Canonical order matches blessed: C-, then M-, then S-, then base.
    return (ctrl ? 'C-' : '') + (meta ? 'M-' : '') + (shift ? 'S-' : '') + base;
}

export function formatKeyLabel(spec) {
    if (typeof spec !== 'string') return '';
    return spec.split('+').map(function (p) { return p.trim(); }).filter(function (p) { return p !== ''; }).join('+').toUpperCase();
}

// --- Section 2: working directory selector ---
export function resolveCwdPromptFlag(command) {
    if (typeof command !== 'object' || command === null) return false;
    return command.promptCwd === true
        || command.cwdPrompt === true
        || command.askCwd === true
        || command.cwdRequired === true;
}

export function listSubdirectories(dir) {
    try {
        const dirents = fs.readdirSync(dir, { withFileTypes: true });
        return dirents
            .filter((d) => {
                try {
                    return d.isDirectory();
                } catch {
                    return false;
                }
            })
            .map((d) => d.name)
            .sort((a, b) => a.localeCompare(b, undefined, { sensitivity: 'base' }));
    } catch {
        return null;
    }
}

export function withGnomeTerminalWorkdir(command, cwd) {
    if (!cwd || typeof command !== 'string') return command;
    if (!command.includes('gnome-terminal')) return command;
    if (command.includes('--working-directory')) return command;
    const escaped = String(cwd).replace(/"/g, '\\"');
    return command.replace('gnome-terminal', `gnome-terminal --working-directory="${escaped}"`);
}

export function resolveSpawnCwd(cwd) {
    if (typeof cwd !== 'string' || cwd.trim() === '') return undefined;
    try {
        const resolved = path.resolve(cwd);
        if (fs.statSync(resolved).isDirectory()) return resolved;
    } catch { /* invalid cwd -> ignore */ }
    return undefined;
}

export function promptForWorkingDirectory(screen, { title = '', initialDir = process.cwd(), theme = {}, scheduleRender = null, fallbackFocus = null } = {}) {
    return new Promise((resolve) => {
        let currentDir = path.resolve(initialDir || process.cwd());
        try {
            if (!fs.statSync(currentDir).isDirectory()) currentDir = process.cwd();
        } catch {
            currentDir = process.cwd();
        }

        const previousFocus = screen.focused;
        const render = typeof scheduleRender === 'function'
            ? scheduleRender
            : () => { try { screen.render(); } catch { /* ignore */ } };

        const overlay = blessed.box({
            parent: screen,
            top: 'center',
            left: 'center',
            width: '70%',
            height: '70%',
            border: 'line',
            label: ` Working directory ${title ? '- ' + String(title).slice(0, 40) : ''} `,
            style: {
                bg: theme.panel,
                fg: theme.text,
                border: { fg: theme.focus }
            }
        });

        const pathBox = blessed.box({
            parent: overlay,
            top: 1,
            left: 1,
            right: 1,
            height: 3,
            border: 'line',
            label: ' Current directory ',
            tags: true,
            style: {
                bg: theme.panelAlt,
                fg: theme.text,
                border: { fg: theme.border }
            }
        });

        const dirList = blessed.list({
            parent: overlay,
            top: 5,
            left: 1,
            right: 1,
            bottom: 2,
            keys: true,
            mouse: true,
            vi: true,
            style: {
                bg: theme.panel,
                fg: theme.text,
                selected: { bg: theme.selected, fg: theme.text },
                item: { hover: { bg: theme.hover, fg: theme.text } }
            }
        });

        blessed.box({
            parent: overlay,
            bottom: 0,
            left: 1,
            right: 1,
            height: 1,
            content: 'Enter: confirm/enter | Backspace: parent | Escape: cancel',
            align: 'center',
            style: { bg: theme.panel, fg: theme.border }
        });

        let items = [];
        let settled = false;

        function cleanup(value) {
            if (settled) return;
            settled = true;
            try {
                overlay.detach();
            } catch { /* ignore */ }
            render();
            try {
                if (previousFocus && typeof previousFocus.focus === 'function') {
                    previousFocus.focus();
                } else if (fallbackFocus && typeof fallbackFocus.focus === 'function') {
                    fallbackFocus.focus();
                }
            } catch { /* ignore */ }
            try {
                screen.render();
            } catch { /* ignore */ }
            resolve(value);
        }

        function refresh() {
            pathBox.setContent(` {bold}${currentDir}{/bold}`);
            const subdirs = listSubdirectories(currentDir);
            items = [{ kind: 'select', name: '', display: `✔ Select this folder` }];
            if (path.dirname(currentDir) !== currentDir) {
                items.push({ kind: 'parent', name: '..', display: '⬆ .. (parent folder)' });
            }
            if (subdirs === null) {
                items.push({ kind: 'info', name: '', display: '(unreadable)' });
            } else {
                for (const name of subdirs) {
                    const prefix = name.startsWith('.') ? '◌ ' : '📁 ';
                    items.push({ kind: 'dir', name, display: `${prefix}${name}` });
                }
            }
            dirList.setItems(items.map((it) => it.display));
            dirList.select(0);
            try {
                screen.render();
            } catch { /* ignore */ }
        }

        dirList.on('select', (_, index) => {
            const item = items[index];
            if (!item) return;
            if (item.kind === 'select') {
                cleanup(currentDir);
                return;
            }
            if (item.kind === 'info') return;
            if (item.kind === 'parent') {
                currentDir = path.dirname(currentDir);
                refresh();
                return;
            }
            if (item.kind === 'dir') {
                const next = path.join(currentDir, item.name);
                try {
                    if (fs.statSync(next).isDirectory()) {
                        currentDir = next;
                        refresh();
                    }
                } catch {
                    // stay on current dir if unreadable
                }
            }
        });

        dirList.key(['backspace'], () => {
            const parent = path.dirname(currentDir);
            if (parent !== currentDir) {
                currentDir = parent;
                refresh();
            }
        });

        dirList.key(['escape'], () => {
            cleanup(null);
        });

        overlay.key(['escape'], () => {
            cleanup(null);
        });

        refresh();
        dirList.focus();
        try {
            screen.render();
        } catch { /* ignore */ }
    });
}
