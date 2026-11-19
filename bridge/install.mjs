#!/usr/bin/env node
// Register bridge/hook.sh in ~/.claude/settings.json (backup first), or remove it with --uninstall.
// Idempotent: entries are matched by the hook's command path.
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
