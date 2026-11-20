#!/usr/bin/env node
// Register bridge/hook.sh in ~/.claude/settings.json, ~/.codex/hooks.json and ~/.copilot/settings.json (backups
// first), or remove it with --uninstall. Idempotent: entries are matched by the hook's command.
import { copyFileSync, readFileSync, writeFileSync } from 'node:fs';
import { homedir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const HOOK = join(dirname(fileURLToPath(import.meta.url)), 'hook.sh');
const SETTINGS = join(homedir(), '.claude', 'settings.json');
const EVENTS = ['SessionStart', 'SessionEnd', 'UserPromptSubmit', 'PreToolUse', 'PostToolUse', 'PostToolUseFailure',
                'Stop', 'PermissionRequest'];
const uninstall = process.argv.includes('--uninstall');

const cfg = JSON.parse(readFileSync(SETTINGS, 'utf8'));
copyFileSync(SETTINGS, `${SETTINGS}.dial-bridge.bak`);
cfg.hooks ??= {};
for (const ev of EVENTS) {
  const list = (cfg.hooks[ev] ??= []).filter((m) => !m.hooks?.some((h) => h.command === HOOK));
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
// Code's nested {matcher, hooks} groups; Copilot sends Claude-shaped payloads for PascalCase event names (its own
// camelCase names get its own payload shape). async: Codex doesn't wait on it; Codex asks once to trust a new hook.
const BUSY_EVENTS = ['SessionStart', 'SessionEnd', 'UserPromptSubmit', 'PreToolUse', 'PostToolUse', 'Stop'];
for (const [file, agent, extra] of [
  [join(homedir(), '.codex', 'hooks.json'), 'codex', { async: true }],
  [join(homedir(), '.copilot', 'settings.json'), 'copilot', {}],
]) {
  const command = `${HOOK} ${agent}`;
  let c = { hooks: {} };
  try { c = JSON.parse(readFileSync(file, 'utf8')); copyFileSync(file, `${file}.dial-bridge.bak`); } catch {}
  c.hooks ??= {};
  for (const ev of BUSY_EVENTS) {
    const list = (c.hooks[ev] ??= []).filter((m) => !m.hooks?.some((h) => h.command === command));
    if (!uninstall) list.push({ hooks: [{ type: 'command', command, timeout: 5, ...extra }] });
    if (list.length) c.hooks[ev] = list;
    else delete c.hooks[ev];
  }
  if (!Object.keys(c.hooks).length) delete c.hooks;
  writeFileSync(file, JSON.stringify(c, null, 2) + '\n');
  console.log(`${uninstall ? 'Removed' : 'Installed'} ${command} for ${BUSY_EVENTS.join(', ')} in ${file}`);
}
