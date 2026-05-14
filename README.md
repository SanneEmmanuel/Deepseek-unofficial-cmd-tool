# Deepseek-unofficial-cmd-tool
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>DeepSeek API – Unofficial C Client</title>

<style>
    *{
        margin:0;
        padding:0;
        box-sizing:border-box;
    }

    body{
        background:#0d1117;
        color:#e6edf3;
        font-family:Arial, Helvetica, sans-serif;
        line-height:1.8;
    }

    .container{
        width:90%;
        max-width:1200px;
        margin:auto;
        padding:40px 20px;
    }

    header{
        text-align:center;
        padding:80px 20px;
        border-bottom:1px solid #30363d;
    }

    header h1{
        font-size:3rem;
        margin-bottom:20px;
        color:#58a6ff;
    }

    header p{
        font-size:1.2rem;
        color:#8b949e;
    }

    section{
        margin-top:60px;
    }

    h2{
        color:#58a6ff;
        margin-bottom:20px;
        border-left:5px solid #58a6ff;
        padding-left:15px;
    }

    p{
        margin-bottom:20px;
    }

    ul{
        margin-left:30px;
        margin-bottom:20px;
    }

    li{
        margin-bottom:10px;
    }

    .card{
        background:#161b22;
        padding:25px;
        border-radius:12px;
        border:1px solid #30363d;
        margin-bottom:25px;
    }

    code{
        background:#21262d;
        padding:2px 6px;
        border-radius:5px;
        color:#79c0ff;
    }

    pre{
        background:#161b22;
        padding:20px;
        overflow:auto;
        border-radius:12px;
        border:1px solid #30363d;
        margin-top:15px;
        margin-bottom:25px;
    }

    .footer{
        text-align:center;
        margin-top:80px;
        padding:30px;
        border-top:1px solid #30363d;
        color:#8b949e;
    }

    .badge{
        display:inline-block;
        padding:8px 15px;
        background:#238636;
        border-radius:30px;
        margin:5px;
        font-size:0.9rem;
    }

    .warning{
        background:#2d1b00;
        border:1px solid #9e6a03;
        padding:20px;
        border-radius:10px;
        margin-top:20px;
    }

</style>
</head>

<body>

<header>
    <h1>DeepSeek API – Unofficial C Client</h1>
    <p>
        Reverse-engineered command-line interface for DeepSeek Web Chat
    </p>

    <div style="margin-top:25px;">
        <span class="badge">C Language</span>
        <span class="badge">SSE Streaming</span>
        <span class="badge">WASM Solver</span>
        <span class="badge">libcurl</span>
        <span class="badge">Wasmtime</span>
    </div>
</header>

<div class="container">

<section>
    <h2>Overview</h2>

    <div class="card">
        <p>
            DeepSeek API – Unofficial C Client is a lightweight native implementation
            of the DeepSeek web communication system written entirely in C.
        </p>

        <p>
            The project reverse-engineers the official DeepSeek browser interface
            and reproduces the request flow directly from a terminal environment
            without requiring browser automation during runtime.
        </p>

        <p>
            Developed by <strong>Sanne Karibo</strong> for educational research,
            networking analysis, systems programming, and protocol experimentation.
        </p>
    </div>
</section>

<section>
    <h2>Features</h2>

    <div class="card">
        <ul>
            <li>Native C implementation</li>
            <li>Real-time streaming responses</li>
            <li>Server-Sent Events (SSE) parser</li>
            <li>Automatic chat session creation</li>
            <li>WebAssembly Proof-of-Work solver</li>
            <li>Markdown-friendly AI responses</li>
            <li>Session persistence</li>
            <li>DeepSeek V3 support</li>
            <li>R1 reasoning compatibility foundation</li>
            <li>Low memory footprint</li>
            <li>No browser dependency during execution</li>
        </ul>
    </div>
</section>

<section>
    <h2>Project Architecture</h2>

    <div class="card">
        <p>The client reproduces the official DeepSeek request pipeline:</p>

        <ul>
            <li>Create chat session</li>
            <li>Request PoW challenge</li>
            <li>Execute challenge solver through WASM</li>
            <li>Generate authenticated PoW response</li>
            <li>Submit completion request</li>
            <li>Receive streamed SSE events</li>
            <li>Parse and display incremental responses</li>
        </ul>
    </div>
</section>

<section>
    <h2>Dependencies</h2>

    <div class="card">
        <ul>
            <li><strong>libcurl</strong> – HTTP networking</li>
            <li><strong>cJSON</strong> – JSON parsing</li>
            <li><strong>Wasmtime</strong> – WebAssembly runtime</li>
            <li><strong>GCC</strong> – C compiler</li>
        </ul>
    </div>
</section>

<section>
    <h2>Compilation</h2>

    <div class="card">

        <p>Install required packages on Linux:</p>

<pre>
sudo apt update

sudo apt install \
    build-essential \
    gcc \
    libcurl4-openssl-dev \
    libcjson-dev
</pre>

        <p>
            Install Wasmtime from:
        </p>

        <p>
            https://wasmtime.dev
        </p>

        <p>Compile the project:</p>

<pre>
gcc deepseek.c -o deepseek \
    -lcurl \
    -lcjson \
    -lwasmtime
</pre>

        <p>
            Make sure <code>wasm.wasm</code> exists in the same directory.
        </p>

    </div>
</section>

<section>
    <h2>Authentication</h2>

    <div class="card">

        <p>
            The client requires two authentication values from your
            DeepSeek account:
        </p>

        <ul>
            <li><code>ds_session_id</code></li>
            <li><code>authorization token</code></li>
        </ul>

        <p>To obtain them:</p>

        <ol style="margin-left:25px;">
            <li>Login to chat.deepseek.com</li>
            <li>Open browser developer tools</li>
            <li>Open Network tab</li>
            <li>Send a message</li>
            <li>Inspect the completion request</li>
            <li>Copy:
                <ul>
                    <li>Cookie → ds_session_id</li>
                    <li>Authorization → Bearer token</li>
                </ul>
            </li>
        </ol>

    </div>
</section>

<section>
    <h2>Initialization</h2>

    <div class="card">

        <p>
            Save your session and token:
        </p>

<pre>
./deepseek init YOUR_DS_SESSION YOUR_BEARER_TOKEN
</pre>

        <p>
            This creates:
        </p>

<pre>
~/.deepseek_config
</pre>

    </div>
</section>

<section>
    <h2>Usage</h2>

    <div class="card">

        <p>Send a prompt:</p>

<pre>
./deepseek "Explain quantum computing"
</pre>

        <p>Example:</p>

<pre>
./deepseek "Write a Linux shell script for backups"
</pre>

        <p>
            Responses stream directly to the terminal in real time.
        </p>

    </div>
</section>

<section>
    <h2>Streaming Engine</h2>

    <div class="card">

        <p>
            The client includes a fully native SSE parser capable of:
        </p>

        <ul>
            <li>Handling incremental response fragments</li>
            <li>Tracking token usage</li>
            <li>Parsing thinking streams</li>
            <li>Managing search citations</li>
            <li>Maintaining message relationships</li>
            <li>Capturing conversation titles</li>
        </ul>

    </div>
</section>

<section>
    <h2>Security Notice</h2>

    <div class="warning">
        <p>
            This project is unofficial and not affiliated with DeepSeek.
        </p>

        <p>
            Your session token grants access to your account session.
            Keep it private and never share your configuration file publicly.
        </p>

        <p>
            Tokens may expire or become invalid if the platform changes
            authentication methods.
        </p>
    </div>
</section>

<section>
    <h2>License</h2>

    <div class="card">

        <p>
            Licensed under the GNU Affero General Public License v3.0.
        </p>

        <p>
            This project is distributed for educational and research purposes
            without warranty of any kind.
        </p>

    </div>
</section>

<div class="footer">
    <p>
        DeepSeek API – Unofficial C Client
    </p>

    <p>
        Copyright © 2025 Sanne Karibo
    </p>
</div>

</div>

</body>
</html>
