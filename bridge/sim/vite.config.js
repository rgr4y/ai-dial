// Simulator dev server: `npm run dev` in bridge/. Hot reload on sim/*.
// Pet art and tables come straight from docs/harness-pets (decoded from Harness pets.c), served as /data.js and /assets.
// /ws is proxied to the dial-bridge daemon, which relays the dial's screen state and forwards taps.
import { fileURLToPath } from 'node:url';

export default {
  root: fileURLToPath(new URL('.', import.meta.url)),
  publicDir: fileURLToPath(new URL('../../docs/harness-pets', import.meta.url)),
  server: {
    port: 5180,
    proxy: { '/ws': { target: `ws://127.0.0.1:${process.env.DIAL_BRIDGE_PORT || 19876}`, ws: true } },
  },
};
