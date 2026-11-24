#!/usr/bin/env node
// Register bridge/hook.sh in ~/.claude/settings.json, ~/.codex/hooks.json, ~/.copilot/settings.json and
// ~/.copilot/hooks/dial-bridge.json (backups first), or remove it with --uninstall. Idempotent: entries are matched
// by the hook's command.
//
// Two Copilot files on purpose:
//   - ~/.copilot/settings.json  is the Copilot CLI's file (its own camelCase event names).
//   - ~/.copilot/hooks/*.json   is what VS Code's Copilot Chat reads (chat.hookFilesLocations, the "copilot-personal"
//     source), with Claude-shaped payloads on PascalCase events. VS Code does not read the CLI file, so the dial
//     would otherwise never see a Copilot tool in an editor window.
import { copyFileSync, readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { homedir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const HOOK = join(dirname(fileURLToPath(import.meta.url)), 'hook.sh');
// One of our entries: the canonical hook, or a stale capture wrapper (`*hookcap*.sh`). Never someone else's hook.
const isOurs = (c) => { const exe = (c ?? '').split(/\s+/)[0]; return exe === HOOK || /hookcap/.test(exe); };
const SETTINGS = join(homedir(), '.claude', 'settings.json');
const EVENTS = ['SessionStart', 'SessionEnd', 'UserPromptSubmit', 'PreToolUse', 'PostToolUse', 'PostToolUseFailure',
                'Stop', 'PermissionRequest'];
const uninstall = process.argv.includes('--uninstall');

const cfg = JSON.parse(readFileSync(SETTINGS, 'utf8'));
copyFileSync(SETTINGS, `${SETTINGS}.dial-bridge.bak`);
cfg.hooks ??= {};
// Drop any of ours first (nested {hooks:[{command}]} shape), matched by the hook file so a stale path never
// accumulates, then add exactly one canonical command.
const oursC = (h) => isOurs(h.command);
for (const ev of EVENTS) {
  const list = (cfg.hooks[ev] ??= []).filter((m) => !m.hooks?.some(oursC));
  if (!uninstall) {
    // PermissionRequest waits for the dial (Claude Code's prompt shows meanwhile); everything else is a 1 s post.
    list.push({ matcher: '', hooks: [{ type: 'command', command: HOOK, timeout: ev === 'PermissionRequest' ? 300 : 5 }] });
  }
  if (list.length) cfg.hooks[ev] = list;
  else delete cfg.hooks[ev];
}
writeFileSync(SETTINGS, JSON.stringify(cfg, null, 2) + '\n');
console.log(`${uninstall ? 'Removed' : 'Installed'} ${HOOK} for ${EVENTS.join(', ')} in ${SETTINGS} (backup: settings.json.dial-bridge.bak)`);

// Codex and Copilot CLI: the same hook tagged with the agent, busy-face events only (no PermissionRequest: hook.sh
// answers in Claude Code's format, and Copilot's permissionRequest payload isn't Claude-shaped). Both take Claude
// Code's nested {matcher, hooks} groups. async: Codex doesn't wait on it; Codex asks once to trust a new hook.
const BUSY_EVENTS = ['SessionStart', 'SessionEnd', 'UserPromptSubmit', 'PreToolUse', 'PostToolUse', 'Stop'];
for (const [file, agent, extra] of [
  [join(homedir(), '.codex', 'hooks.json'), 'codex', { async: true }],
  [join(homedir(), '.copilot', 'settings.json'), 'copilot', {}],
]) {
  const command = `${HOOK} ${agent}`;
  let c = { hooks: {} };
  try { c = JSON.parse(readFileSync(file, 'utf8')); copyFileSync(file, `${file}.dial-bridge.bak`); } catch {}
  c.hooks ??= {};
  // Drop any of ours first (nested {hooks:[{command}]} shape here), matched by the hook file so a stale path
  // (an old wrapper, a moved checkout) never accumulates, then add exactly one canonical command.
  const ours = (h) => isOurs(h.command);
  for (const ev of BUSY_EVENTS) {
    const list = (c.hooks[ev] ??= []).filter((m) => !m.hooks?.some(ours));
    if (!uninstall) list.push({ hooks: [{ type: 'command', command, timeout: 5, ...extra }] });
    if (list.length) c.hooks[ev] = list;
    else delete c.hooks[ev];
  }
  if (!Object.keys(c.hooks).length) delete c.hooks;
  writeFileSync(file, JSON.stringify(c, null, 2) + '\n');
  console.log(`${uninstall ? 'Removed' : 'Installed'} ${command} for ${BUSY_EVENTS.join(', ')} in ${file}`);
}

// VS Code Copilot Chat (the extension, not the CLI): its own hook file, in the location chat.hookFilesLocations
// reads. Busy events tag the agent; PreToolUse additionally runs the gate (hook.sh copilot-gate), so a mutating tool
// asks the dial. One file of ours: never edit someone else's ~/.copilot/hooks/*.json.
const VSCODE_FILE = join(homedir(), '.copilot', 'hooks', 'dial-bridge.json');
mkdirSync(dirname(VSCODE_FILE), { recursive: true });
let v = { version: 1, hooks: {} };
try { v = JSON.parse(readFileSync(VSCODE_FILE, 'utf8')); copyFileSync(VSCODE_FILE, `${VSCODE_FILE}.dial-bridge.bak`); } catch {}
v.version ??= 1;
v.hooks ??= {};
for (const ev of BUSY_EVENTS) {
  // This file's entries put `command` on the item itself (flat), unlike ~/.claude's nested {matcher, hooks} groups.
  // Drop any of ours for this event first (matched by the hook file, so a stale wrapper or an old path never
  // accumulates), then add exactly one canonical command.
  const arg = ev === 'PreToolUse' ? 'copilot-gate' : 'copilot';
  const command = `${HOOK} ${arg}`;
  const ours = (m) => isOurs(m.command);
  const list = (v.hooks[ev] ??= []).filter((m) => !ours(m));
  if (!uninstall) {
    // The gate holds for the dial; the rest are a 1 s post (the gate also carries the busy event, so one entry).
    list.push({ type: 'command', command, timeout: ev === 'PreToolUse' ? 300 : 5 });
  }
  if (list.length) v.hooks[ev] = list;
  else delete v.hooks[ev];
}
if (!Object.keys(v.hooks).length && uninstall) { try { writeFileSync(VSCODE_FILE, JSON.stringify({ version: 1, hooks: {} }, null, 2) + '\n'); } catch {} }
else writeFileSync(VSCODE_FILE, JSON.stringify(v, null, 2) + '\n');
console.log(`${uninstall ? 'Removed' : 'Installed'} VS Code Copilot hooks in ${VSCODE_FILE} (PreToolUse runs the gate)`);
