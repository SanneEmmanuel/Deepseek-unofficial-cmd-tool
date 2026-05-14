# ⚠️ Unofficial DeepSeek Cmd tool and Client – 

> **Legal & Ethical Notice**  
> This project is **unofficial**, **not endorsed**, and **not affiliated** with DeepSeek (深度求索).  
> It is provided **for educational and research purposes only** – to study networking, WebAssembly challenges, and Server‑Sent Events in a terminal environment.  
>  
> **Using this client may violate DeepSeek’s Terms of Service.**  
> You are solely responsible for how you use it. The author assumes **no liability** for account bans, legal consequences, or any other damages.  
>  
> Always respect the platform’s rate limits, do not overload their services, and stop using this tool immediately if it conflicts with DeepSeek’s official policies.

---

# DeepSeek API – Unofficial C Client

**Reverse‑engineered command‑line interface for DeepSeek Web Chat**  
Written entirely in C by Sanne Karibo. No browser automation at runtime.

![C Language](https://img.shields.io/badge/C-99-blue) ![SSE Streaming](https://img.shields.io/badge/SSE-Streaming-brightgreen) ![WASM Solver](https://img.shields.io/badge/WASM-PoW-orange) ![libcurl](https://img.shields.io/badge/libcurl-HTTP-yellow) ![Wasmtime](https://img.shields.io/badge/Wasmtime-Runtime-red)

## Overview

This is a lightweight native implementation that reproduces the request flow of the official DeepSeek browser interface.  
It handles session creation, Proof‑of‑Work challenges (via WebAssembly), and real‑time SSE streaming – all from a terminal.

**Developed by Sanne Karibo** for networking analysis, systems programming, and protocol experimentation.

## Features

- Native C implementation (minimal dependencies)
- Real‑time streaming responses (Server‑Sent Events)
- Automatic chat session creation & persistence
- WebAssembly Proof‑of‑Work solver (using Wasmtime)
- Markdown‑friendly AI responses
- DeepSeek V3 support & R1 reasoning foundation
- Low memory footprint – no browser needed during execution

## Architecture

The client replicates the official DeepSeek pipeline:

1. Create a chat session  
2. Request a PoW challenge  
3. Execute the WASM solver  
4. Generate an authenticated PoW response  
5. Submit the completion request  
6. Receive and parse SSE events  
7. Display incremental responses

## Dependencies

- **libcurl** – HTTP networking  
- **cJSON** – JSON parsing  
- **Wasmtime** – WebAssembly runtime  
- **GCC** – C compiler

## Compilation (Linux)

Install required system packages:

```bash
sudo apt update
sudo apt install build-essential gcc libcurl4-openssl-dev libcjson-dev
```

Install Wasmtime from [https://wasmtime.dev](https://wasmtime.dev) or via your package manager.

Compile the project:

```bash
gcc deepseek.c -o deepseek -lcurl -lcjson -lwasmtime
```

Make sure the file `wasm.wasm` is present in the same directory.

## Authentication – Obtaining Session & Token

The client needs two values from your DeepSeek account:

- `ds_session_id` (from cookies)  
- `authorization` token (Bearer)

**How to obtain them:**

1. Log into [chat.deepseek.com](https://chat.deepseek.com)  
2. Open browser developer tools (F12)  
3. Go to the **Network** tab  
4. Send a message in the chat  
5. Find the `completion` request  
6. Copy:
   - `Cookie` header → `ds_session_id=...`
   - `Authorization` header → `Bearer ...` (the token part)

> ⚠️ **Security** – These credentials grant access to your DeepSeek session.  
> Keep them private. Never commit or share your configuration file.

## Initialization

Save your session and token:

```bash
./deepseek init YOUR_DS_SESSION YOUR_BEARER_TOKEN
```

This creates `~/.deepseek_config` with your credentials.

## Usage

Send a prompt:

```bash
./deepseek "Explain quantum computing"
```

Example:

```bash
./deepseek "Write a Linux shell script for backups"
```

Responses stream directly to the terminal in real time.

## Streaming Engine

The native SSE parser handles:

- Incremental response fragments
- Token usage tracking
- Thinking / reasoning streams
- Search citations
- Message relationships
- Conversation titles

## ⚠️ Important Legal & Safety Warnings

1. **Unofficial nature** – This project is not created, approved, or maintained by DeepSeek.  
2. **Terms of Service** – Using automated clients may violate DeepSeek’s ToS. You risk account suspension or permanent ban.  
3. **No warranty** – The software is provided “AS IS”, without any warranties. The author is not liable for any damage or loss.  
4. **Rate limits** – Do not spam or overload DeepSeek’s infrastructure.  
5. **Educational use only** – You are expected to study networking, WASM, and SSE; not to circumvent security or monetize the API.  
6. **Token expiration** – Session tokens may expire or become invalid when DeepSeek changes its authentication flow.  
7. **Respect copyright** – Do not use this to extract or redistribute DeepSeek’s proprietary content in violation of their rights.

By using this software, you agree that you have read this notice and that you assume full responsibility for any consequences.

## License

Copyright © 2025 Sanne Karibo  

This program is free software: you can redistribute it and/or modify it under the terms of the **GNU Affero General Public License v3.0** (AGPL-3.0).  

This program is distributed in the hope that it will be useful, but **WITHOUT ANY WARRANTY**; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the AGPL-3.0 license for more details.

A copy of the license is available at [https://www.gnu.org/licenses/agpl-3.0.html](https://www.gnu.org/licenses/agpl-3.0.html).

---

**Use responsibly. Respect the platform. Code for learning, not for abuse.**
