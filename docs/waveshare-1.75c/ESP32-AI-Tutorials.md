# ESP32-S3-Touch-AMOLED-1.75C AI Application Tutorial

Welcome to the Waveshare AI Application Tutorials!

This tutorial series is designed to help developers master the quick setup of AI agents on Waveshare's ESP32 series development boards that support AI applications. The tutorial includes application guides such as XiaoZhi AI, OpenClaw, and ESP-Claw. You can choose the corresponding tutorial based on your needs.

- [XiaoZhi AI Application Tutorial](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75C/XiaoZhi_AI)

------------------------------------------------------------------------

### XiaoZhi AI Supported Products List

![](data:image/svg+xml;base64,PHN2ZyB2aWV3Ym94PSIwIDAgMTQgMTYiPjxwYXRoIGZpbGwtcnVsZT0iZXZlbm9kZCIgZD0iTTcgMi4zYzMuMTQgMCA1LjcgMi41NiA1LjcgNS43cy0yLjU2IDUuNy01LjcgNS43QTUuNzEgNS43MSAwIDAgMSAxLjMgOGMwLTMuMTQgMi41Ni01LjcgNS43LTUuN3pNNyAxQzMuMTQgMSAwIDQuMTQgMCA4czMuMTQgNyA3IDcgNy0zLjE0IDctNy0zLjE0LTctNy03em0xIDNINnY1aDJWNHptMCA2SDZ2Mmgydi0yeiIgLz48L3N2Zz4=)info

Firmware for some models is universal. For example, the ESP32-S3-LCD-1.85 and ESP32-S3-Touch-LCD-1.85 can use the same firmware.

![](data:image/svg+xml;base64,PHN2ZyB2aWV3Ym94PSIwIDAgMTQgMTYiPjxwYXRoIGZpbGwtcnVsZT0iZXZlbm9kZCIgZD0iTTcgMi4zYzMuMTQgMCA1LjcgMi41NiA1LjcgNS43cy0yLjU2IDUuNy01LjcgNS43QTUuNzEgNS43MSAwIDAgMSAxLjMgOGMwLTMuMTQgMi41Ni01LjcgNS43LTUuN3pNNyAxQzMuMTQgMSAwIDQuMTQgMCA4czMuMTQgNyA3IDcgNy0zLjE0IDctNy0zLjE0LTctNy03em0xIDNINnY1aDJWNHptMCA2SDZ2Mmgydi0yeiIgLz48L3N2Zz4=)info

Some models have V1 and V2 versions. Download the corresponding firmware version based on the product.

| Model | Firmware Version | Location | Notes |
|----|----|----|----|
| ESP32-C6-Touch-AMOLED-1.32 | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-C6-Touch-AMOLED-1.43 | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-C6-LCD-1.69 | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-C6-Touch-LCD-1.83 | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-C6-Touch-AMOLED-1.8 | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-C6-Touch-AMOLED-2.06 | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-C6-Touch-AMOLED-2.16 | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-P4-NANO-10.1-A | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-P4-WIFI6-Touch-LCD-3.4C | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-P4-WIFI6-Touch-LCD-10.1 | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-P4-WIFI6-Touch-LCD-4B | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-P4-WIFI6-Touch-LCD-4C | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-P4-WIFI6-Touch-LCD-7 | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-P4-WIFI6-Touch-LCD-7B | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-P4-WIFI6-Touch-LCD-8 | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-P4-WIFI6-Touch-LCD-3.5 | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-P4-WIFI6-Touch-LCD-4.3 | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-P4-WIFI6-Touch-LCD-5 | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-S3-Touch-AMOLED-1.32 | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-S3-Touch-LCD-3.49 | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-S3-ePaper-1.54 | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | Firmware is available in v1 and v2 versions; download the corresponding version based on the product |
| ESP32-S3-Touch-AMOLED-1.75 | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-S3-Touch-AMOLED-1.75C | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-S3-Touch-LCD-1.83 | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-S3-Touch-LCD-1.85C-BOX | v2.4.0 | [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | Firmware is available in v1 and v2 versions; download the corresponding version based on the product |
| ESP32-S3-LCD-1.85 | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-S3-Touch-LCD-1.85 | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-S3-Touch-AMOLED-1.8 | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-S3-Touch-AMOLED-2.06 | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-S3-Touch-LCD-3.5B | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-S3-Touch-LCD-3.5 | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-S3-RLCD-4.2 | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-S3-AUDIO-Board | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-S3-Touch-LCD-4B | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-S3-Touch-LCD-4.3C | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-S3-Touch-LCD-1.46 | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-S3-Touch-LCD-1.54 | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-S3-LCD-0.85 | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-S3-ePaper-3.97 | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-S3-Touch-AMOLED-1.43C | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-S3-Touch-AMOLED-2.16 | v2.4.0 | [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-S3-CAM-XX | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | Supports connecting to Waveshare display modules such as 3.5", 2.8", 2", 1.83", etc. Different firmware corresponds to different display modules. |
| ESP32-S3-Touch-LCD-1.85B | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-S3-RGB-Matrix | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-S3-Touch-LCD-7C-BOX | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |
| ESP32-Touch-LCD-3.5 | v2.4.0 | [XiaoZhi GitHub](https://github.com/78/xiaozhi-esp32/releases) or [Waveshare GitHub](https://github.com/waveshareteam/ESP32-AIChats/tree/master/xiaozhi-esp32) | \- |

------------------------------------------------------------------------

[![](data:image/svg+xml;base64,PHN2ZyBjbGFzcz0iZmVlZGJhY2stYnV0dG9uX19pY29uIiB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIxNiIgaGVpZ2h0PSIxNiIgdmlld2JveD0iMCAwIDI0IDI0IiBmaWxsPSJub25lIiBzdHJva2U9ImN1cnJlbnRDb2xvciIgc3Ryb2tlLXdpZHRoPSIyIiBzdHJva2UtbGluZWNhcD0icm91bmQiIHN0cm9rZS1saW5lam9pbj0icm91bmQiIGFyaWEtaGlkZGVuPSJ0cnVlIj48cGF0aCBkPSJNMjEgMTVhMiAyIDAgMCAxLTIgMkg3bC00IDRWNWEyIDIgMCAwIDEgMi0yaDE0YTIgMiAwIDAgMSAyIDJ6IiAvPjwvc3ZnPg==)Give Feedback](https://docs.google.com/forms/d/e/1FAIpQLSfayJEZ5J-dp-3Wq_dkWsVRhNuQ6C79_GNV62mYuFW8Mj6U8Q/viewform)
