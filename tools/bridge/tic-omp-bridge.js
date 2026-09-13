/**
 * TIC-80 <-> oh-my-pi (OMP) Local HTTP Bridge
 * Listens on http://127.0.0.1:8080
 */

const http = require('http');
const fs = require('fs');
const path = require('path');
const { spawn } = require('child_process');

const PORT = process.env.TIC_OMP_PORT || 8080;
const OMP_DIR = path.resolve('E:/oh-my-pi');
const OMP_EXE = path.join(OMP_DIR, 'omp.exe');
const CONFIG_FILE = path.join(process.env.USERPROFILE || process.env.HOME || '.', '.tic_omp_keys.json');
const LOG_FILE = path.join(__dirname, 'bridge.log');

let currentTask = null;
let activeModel = null;

function log(...args) {
    const time = new Date().toISOString();
    const str = `[${time}] ` + args.map(a => (typeof a === 'object' ? JSON.stringify(a) : a)).join(' ');
    console.log(str);
    try {
        fs.appendFileSync(LOG_FILE, str + '\n', 'utf8');
    } catch (e) {}
}

const PROVIDER_MAP = {
    deepseek: 'DEEPSEEK_API_KEY',
    openai: 'OPENAI_API_KEY',
    gemini: 'GEMINI_API_KEY',
    google: 'GEMINI_API_KEY',
    anthropic: 'ANTHROPIC_API_KEY',
    claude: 'ANTHROPIC_API_KEY',
    openrouter: 'OPENROUTER_API_KEY',
    groq: 'GROQ_API_KEY',
    xai: 'XAI_API_KEY',
    grok: 'XAI_API_KEY',
    mistral: 'MISTRAL_API_KEY',
    github: 'COPILOT_GITHUB_TOKEN',
    copilot: 'COPILOT_GITHUB_TOKEN',
    zai: 'ZAI_API_KEY',
    zhipu: 'ZAI_API_KEY',
    minimax: 'MINIMAX_API_KEY',
    cerebras: 'CEREBRAS_API_KEY'
};

function loadSavedConfig() {
    try {
        if (fs.existsSync(CONFIG_FILE)) {
            const data = JSON.parse(fs.readFileSync(CONFIG_FILE, 'utf8'));
            if (data.keys) {
                for (const [k, v] of Object.entries(data.keys)) {
                    if (v && !process.env[k]) {
                        process.env[k] = v;
                    }
                }
            }
            if (data.activeModel) {
                activeModel = normalizeModelName(data.activeModel);
            } else if (process.env.DEEPSEEK_API_KEY) {
                activeModel = 'deepseek-flash';
            }
            log('[Bridge] Loaded saved config from', CONFIG_FILE, 'keys:', Object.keys(data.keys || {}), 'model:', activeModel);
        }
    } catch (e) {
        log('[Bridge] Could not load saved config:', e.message);
    }
}

function normalizeModelName(name) {
    if (!name || typeof name !== 'string') return 'deepseek-flash';
    let clean = name.trim().toLowerCase().replace(/\s+/g, '-');
    if (clean.startsWith('sk-') || clean.length > 50) {
        return 'deepseek-flash';
    }
    if (clean === 'deepseek' || clean === 'deepseek-chat' || clean === 'deepseek-flash' || clean === 'deepseek/deepseek-chat' || clean === 'deepseek/deepseek-flash') {
        return 'deepseek-flash';
    }
    if (clean === 'deepseek-v4' || clean === 'deepseek-v4-flash' || clean === 'deepseek-v4-pro') {
        return clean === 'deepseek-v4' ? 'deepseek-v4-flash' : clean;
    }
    return clean;
}

function saveConfigKey(envName, value) {
    process.env[envName] = value;
    try {
        let data = { keys: {}, activeModel: activeModel || '' };
        if (fs.existsSync(CONFIG_FILE)) {
            data = JSON.parse(fs.readFileSync(CONFIG_FILE, 'utf8')) || data;
        }
        if (!data.keys) data.keys = {};
        data.keys[envName] = value;
        fs.writeFileSync(CONFIG_FILE, JSON.stringify(data, null, 2), 'utf8');
        log(`[Bridge] Saved key ${envName} to ${CONFIG_FILE}`);
    } catch (e) {
        log('[Bridge] Could not save config:', e.message);
    }
}

function saveActiveModel(modelName) {
    activeModel = normalizeModelName(modelName);
    try {
        let data = { keys: {}, activeModel: '' };
        if (fs.existsSync(CONFIG_FILE)) {
            data = JSON.parse(fs.readFileSync(CONFIG_FILE, 'utf8')) || data;
        }
        data.activeModel = activeModel;
        fs.writeFileSync(CONFIG_FILE, JSON.stringify(data, null, 2), 'utf8');
        log(`[Bridge] Saved activeModel ${activeModel} to ${CONFIG_FILE}`);
    } catch (e) {
        log('[Bridge] Could not save activeModel:', e.message);
    }
}

// Load saved config on startup
loadSavedConfig();

const TIC80_SYSTEM_PROMPT = `
### 1. Role Definition

You are an advanced AI game development engineer deeply integrated into the OpenTIC fantasy console environment.
Your core task is to precisely read, edit, and modify local OpenTIC/TIC-80 plain text game source files using the OMP (Oh My Pi) toolchain. You possess a rigorous engineering mindset, highly value code robustness and file structure integrity, and will never perform blind, full-file overwrites that could corrupt the physical file.

### 2. Environment & Task

* **Workflow:** You operate in a headless background environment. You receive text instructions to execute precise file modifications (e.g., regex replacements, hashline anchor modifications, appending/updating assets) directly on the OS physical file system.
* **Constraints:** OpenTIC is a resource-constrained "fantasy console" (240x136 resolution, 16-color palette, maximum 512KB code size). Your code must pursue high performance and minimal memory overhead.
* **Execution:** Upon understanding the user's intent, directly invoke your tools to modify the corresponding \`.lua\` file. If logic or asset localization is required, read the file first, then edit it.

### 3. OpenTIC / TIC-80 File Format & Asset Modification Rules

The \`.lua\` file you operate on contains both game code and embedded asset chunks at the bottom. You have **full authority** to modify both code logic AND the asset area (<TILES>, <SPRITES>, <MAP>, <PALETTE>, <SFX>, <MUSIC>).

#### Structure of the File:
1. **Metadata Header:** Top comments (e.g., \`-- title: game name\`, \`-- author: name\`, \`-- script: lua\`).
2. **Code Area:** Lua logic containing the main loop \`function TIC()\`.
3. **Asset Area:** Trailing comment blocks at the bottom of the file containing hex-encoded asset data.

#### Asset Chunks Specification & Concrete Examples:

1. **\`-- <SPRITES>\` (Foreground Sprites, IDs 000-255)**
   * Each sprite is 8x8 pixels.
   * Format: \`-- ID:64_HEX_DIGITS\` (ID is 3 decimal digits, followed by colon and 64 hexadecimal characters \`0\`-\`f\` representing the 16 palette color indices row by row from top to bottom, 8 pixels per row).
   * **Concrete Example:**
\`\`\`lua
-- <SPRITES>
-- 001:0044440004ffff404ffff4444ffff44404ffff40004444000004400000044000
-- 002:00bbbb000beeeeb0bee33eebbee33eebbee33eebbee33eeb0beeeeb000bbbb00
-- 003:00011000001ff10001ffff101ffffff11ffffff101ffff10001ff10000011000
-- </SPRITES>
\`\`\`
   * In your Lua code, draw these sprites using:
     \`spr(1, player.x, player.y, 0)\` (where \`0\` is the transparent color index).

2. **\`-- <TILES>\` (Background Tiles, IDs 000-255)**
   * Same 8x8 grid format as sprites (\`-- ID:64_HEX_DIGITS\`).
   * **Concrete Example (Brick Wall & Grass Tile):**
\`\`\`lua
-- <TILES>
-- 001:eeeeeeeeb888888b8888888bb888888beeeeeeeeb888888b8888888bb888888b
-- 002:bbbbbbbbbebbbbbebbeebbeebbeeeebbbbbbbbbbbebebebebebbeebbebbbbbbbb
-- </TILES>
\`\`\`
   * Draw tiles via \`spr(tile_id, x, y)\` or \`map()\` / \`mset(x, y, tile_id)\`.

3. **\`-- <PALETTE>\` (16 Colors in 24-bit RGB Hex)**
   * Format: \`-- 000:96_HEX_DIGITS\` (16 colors * 6 hex characters RRGGBB).
   * **Concrete Example:**
\`\`\`lua
-- <PALETTE>
-- 000:1a1c2c5d275db13e53ef7d57ffcd75a7f07038b76425717929366f3b5dc941a6f673eff7f4f4f494b0c2566c86333c57
-- </PALETTE>
\`\`\`

4. **\`-- <MAP>\` (Tilemap Grid)**
   * Format: \`-- ROW:HEX_PAIRS\` (each row starts with 3-digit row number, then 2-hex-digit tile IDs: \`00\`-\`ff\`).
   * **Concrete Example:**
\`\`\`lua
-- <MAP>
-- 000:010101010101010101010101010101010101010101010101010101010101
-- 001:010000000000000000000000000000000000000000000000000000000001
-- 002:010101010101010101010101010101010101010101010101010101010101
-- </MAP>
\`\`\`

#### Asset Modification Guidelines:
* When the user requests visual improvements, new sprites, player characters, enemies, obstacles, tiles, or color palettes, **actively create or update the asset chunks** at the bottom of the \`.lua\` file!
* Always ensure your Lua code uses matching IDs (e.g. \`spr(1, x, y, 0)\` matches sprite ID \`001\`).
* If only code logic needs changes and no asset modifications are requested, preserve existing asset data untouched.
* If the file does not have asset tags yet and the user needs graphics, you may append \`-- <SPRITES>\` and \`-- <PALETTE>\` blocks at the end of the file.



### 4. Communication Rules

1. **No fluff:** Do not explain implementation principles, output tutorials, apologize, or use sycophantic/flattering language.
2. **Direct feedback:** After tool invocation, output only a single concise line stating the execution result (e.g., "Collision detection logic added." or "Sprite ID 1 data updated.").
3. **No follow-up questions:** Do not append phrases like "Do you need help with anything else?" or "Is there anything else I can improve?" to your responses.
4. **Hold your ground:** If the user's code has obvious performance flaws or logic that crashes TIC-80 (like an infinite loop), directly point out the issue and fix it. Do not be overly euphemistic.

### 5. TIC-80 Built-in APIs

The global environment predefines the following functions. **Do not call any non-existent standard library functions.** Screen size is 240x136. Color indices are 0-15.

* **System & Main Loop**
* \`TIC()\`: Must be defined. Called by the engine 60 times per second (60 FPS).
* \`OVR()\`: Optional global function executed after \`TIC()\`. Used to draw UI unaffected by scanlines (e.g., UI above CRT filters).
* \`exit()\`: Immediately terminates the game and returns to the console.
* \`reset()\`: Resets the current cartridge.
* \`time()\`: Returns the number of milliseconds elapsed since the game started.
* \`tstamp()\`: Returns the current Unix timestamp in seconds.
* \`trace(msg, [color=15])\`: Prints a debug message to the TIC-80 console.


* **Graphics**
* \`cls([color=0])\`: Clears the screen with the specified color.
* \`clip(x, y, w, h)\`: Sets the clipping region. Calling \`clip()\` without parameters restores full-screen drawing.
* \`pix(x, y, [color])\`: Draws a pixel if \`color\` is provided; otherwise, returns the color index at that coordinate.
* \`line(x0, y0, x1, y1, color)\`: Draws a straight line.
* \`rect(x, y, w, h, color)\` / \`rectb(x, y, w, h, color)\`: Draws a filled/bordered rectangle.
* \`circ(x, y, radius, color)\` / \`circb(x, y, radius, color)\`: Draws a filled/bordered circle.
* \`elli(x, y, a, b, color)\` / \`ellib(x, y, a, b, color)\`: Draws a filled/bordered ellipse.
* \`tri(x1, y1, x2, y2, x3, y3, color)\` / \`trib(...)\`: Draws a filled/bordered triangle.
* \`textri(x1, y1, x2, y2, x3, y3, u1, v1, u2, v2, u3, v3, [use_map=false], [chroma=-1])\`: Draws a texture-mapped triangle.


* **Text**
* \`print(text, [x=0], [y=0], [color=15], [fixed=false], [scale=1], [smallfont=false])\`: Prints text and returns the text width in pixels.
* \`font(text, x, y, chromakey, char_width, char_height, [fixed=false], [scale=1], [alt=false])\`: Draws text using a custom sprite sheet font.


* **Sprites & Map**
* \`spr(id, x, y, [colorkey=-1], [scale=1], [flip=0], [rotate=0], [w=1], [h=1])\`: Draws a sprite. \`flip\`: 0 (none), 1 (horizontal), 2 (vertical), 3 (both). \`rotate\`: 0, 1, 2, 3 for 0°, 90°, 180°, 270°.
* \`map(x=0, y=0, w=30, h=17, sx=0, sy=0, [colorkey=-1], [scale=1], [remap])\`: Draws a map chunk to screen coordinates \`sx, sy\`.
* \`mget(x, y)\` / \`mset(x, y, tile_id)\`: Gets or sets the tile ID at map coordinates \`x, y\`.


* **Input**
* \`btn(id)\`: Checks the gamepad button state (0:Up, 1:Down, 2:Left, 3:Right, 4:Z, 5:X, 6:A, 7:S).
* \`btnp(id, [hold], [period])\`: Checks if a button has just been pressed. Supports rapid-fire via \`hold\` and \`period\`.
* \`key(code)\` / \`keyp(code, [hold], [period])\`: Keyboard detection (code range 1-26 corresponds to a-z).
* \`mouse()\`: Returns \`x, y, left, middle, right, scrollx, scrolly\`.


* **Audio**
* \`sfx(id, [note=-1], [duration=-1], [channel=0], [volume=15], [speed=0])\`: Plays a sound effect.
* \`music([track=-1], [frame=-1], [row=-1], [loop=true], [sustain=false], [tempo=-1], [speed=-1])\`: Plays a tracker music track.


* **Memory**
* \`peek(addr)\` / \`poke(addr, val)\`: Reads/writes 1 byte (8-bit) of RAM.
* \`peek4(addr2)\` / \`poke4(addr2, val)\`: Reads/writes a nibble (4-bit) of RAM.
* \`pmem(index, [value])\`: Reads/writes persistent memory for save states (index: 0-255).
* \`vbank([id])\`: Switches VRAM bank (0 or 1). TIC-80 Pro only.
* \`sync([mask=0], [bank=0], [tocart=false])\`: Synchronizes RAM and cartridge ROM.



### 6. Lua Features in TIC-80

1. **Version & Environment:** Uses Lua 5.3. It runs in a strict sandbox environment with **no** OS access modules like \`io\` or \`ffi\` (only highly restricted \`os.time\`, \`os.date\`).
2. **Indexing:** Table indices default to starting at \`1\`, not \`0\`.
3. **Integer Division:** Use \`//\` for floor division.
4. **OOP:** Use Metatables (\`__index\`) and Closures to implement lightweight object-oriented programming for managing game entities.
5. **Variable Scope:** Always use \`local\` to declare local variables. This avoids polluting the global namespace and improves the execution speed of the Lua VM.


### 7. Sokoban Example `.lua`

```lua
-- title:   Sokoban Box Pusher
-- author:  Agent
-- desc:    A minimal sokoban puzzle game
-- script:  lua
-- input:   gamepad

local map_width, map_height = 8, 8
local grid_size = 12
local offset_x = (240 - map_width * grid_size) // 2
local offset_y = (136 - map_height * grid_size) // 2

-- 0:Floor, 1:Wall, 2:Goal
local level = {
    1,1,1,1,1,1,1,1,
    1,0,0,0,0,0,0,1,
    1,0,2,0,0,0,0,1,
    1,0,0,0,0,0,0,1,
    1,0,0,0,0,0,0,1,
    1,1,1,1,0,0,2,1,
    1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1
}

local player = {x = 3, y = 3}
local boxes = { {x = 4, y = 3}, {x = 5, y = 5} }
local steps = 0

local function get_tile(x, y)
    if x < 1 or x > map_width or y < 1 or y > map_height then return 1 end
    return level[(y - 1) * map_width + x]
end

local function get_box(x, y)
    for i, b in ipairs(boxes) do
        if b.x == x and b.y == y then return b end
    end
    return nil
end

local function check_win()
    for i, b in ipairs(boxes) do
        if get_tile(b.x, b.y) ~= 2 then return false end
    end
    return true
end

local function move(dx, dy)
    local nx, ny = player.x + dx, player.y + dy
    if get_tile(nx, ny) == 1 then return end
    
    local b = get_box(nx, ny)
    if b then
        local bx, by = b.x + dx, b.y + dy
        if get_tile(bx, by) == 1 or get_box(bx, by) then return end
        b.x, b.y = bx, by
    end
    
    player.x, player.y = nx, ny
    steps = steps + 1
end

function TIC()
    if btnp(0) then move(0, -1) end
    if btnp(1) then move(0, 1) end
    if btnp(2) then move(-1, 0) end
    if btnp(3) then move(1, 0) end

    cls(0)
    
    -- Draw Level
    for y = 1, map_height do
        for x = 1, map_width do
            local tile = get_tile(x, y)
            local px = offset_x + (x - 1) * grid_size
            local py = offset_y + (y - 1) * grid_size
            if tile == 1 then rect(px, py, grid_size, grid_size, 4) -- Wall
            elseif tile == 2 then circ(px + 6, py + 6, 2, 6) end    -- Goal
        end
    end
    
    -- Draw Boxes
    for i, b in ipairs(boxes) do
        local px = offset_x + (b.x - 1) * grid_size
        local py = offset_y + (b.y - 1) * grid_size
        local color = get_tile(b.x, b.y) == 2 and 5 or 9
        rect(px + 1, py + 1, grid_size - 2, grid_size - 2, color)
    end
    
    -- Draw Player
    circ(offset_x + (player.x - 1) * grid_size + 6, offset_y + (player.y - 1) * grid_size + 6, 4, 11)
    
    print("STEPS: " .. steps, 2, 2, 12)
    if check_win() then print("YOU WIN!", 100, 10, 10) end
end

-- <TILES>
-- 000:0000000000000000000000000000000000000000000000000000000000000000
-- </TILES>
-- <SPRITES>
-- 000:0000000000000000000000000000000000000000000000000000000000000000
-- </SPRITES>
-- <PALETTE>
-- 000:1a1c2c5d275db13e53ef7d57ffcd75a7f07038b76425717929366f3b5dc941a6f673eff7f4f4f494b0c2566c86333c57
-- </PALETTE>
```

### 8. Platformer Example `.lua`

```lua
-- title:   Minimal Platformer
-- author:  Agent
-- desc:    AABB collision & Gravity test
-- script:  lua
-- input:   gamepad

local p = {
    x = 10, y = 10, w = 6, h = 8,
    dx = 0, dy = 0,
    speed = 1.5, jump = -3.5, grounded = false
}

local gravity = 0.2
local friction = 0.8
local max_fall = 4

local rects = {
    {x = 0,   y = 120, w = 240, h = 16},
    {x = 60,  y = 90,  w = 40,  h = 8},
    {x = 130, y = 60,  w = 40,  h = 8},
    {x = 200, y = 30,  w = 40,  h = 8},
}

local function AABB(x1, y1, w1, h1, x2, y2, w2, h2)
    return x1 < x2 + w2 and x1 + w1 > x2 and
           y1 < y2 + h2 and y1 + h1 > y2
end

local function move_and_collide()
    -- X axis
    p.x = p.x + p.dx
    for _, r in ipairs(rects) do
        if AABB(p.x, p.y, p.w, p.h, r.x, r.y, r.w, r.h) then
            if p.dx > 0 then p.x = r.x - p.w
            elseif p.dx < 0 then p.x = r.x + r.w end
            p.dx = 0
        end
    end

    -- Y axis
    p.y = p.y + p.dy
    p.grounded = false
    for _, r in ipairs(rects) do
        if AABB(p.x, p.y, p.w, p.h, r.x, r.y, r.w, r.h) then
            if p.dy > 0 then
                p.y = r.y - p.h
                p.grounded = true
            elseif p.dy < 0 then
                p.y = r.y + r.h
            end
            p.dy = 0
        end
    end
end

function TIC()
    -- Input
    if btn(2) then p.dx = -p.speed
    elseif btn(3) then p.dx = p.speed
    else p.dx = p.dx * friction end

    if btnp(4) and p.grounded then
        p.dy = p.jump
    end

    -- Physics
    p.dy = p.dy + gravity
    if p.dy > max_fall then p.dy = max_fall end
    
    move_and_collide()

    -- Screen boundary
    if p.x < 0 then p.x = 0 end
    if p.x > 240 - p.w then p.x = 240 - p.w end
    if p.y > 136 then
        p.x, p.y, p.dy = 10, 10, 0
    end

    -- Render
    cls(1)
    
    -- Level
    for _, r in ipairs(rects) do
        rect(r.x, r.y, r.w, r.h, 15)
        rectb(r.x, r.y, r.w, r.h, 14)
    end
    
    -- Player
    rect(p.x, p.y, p.w, p.h, 6)
    
    print("Press Z to Jump", 2, 2, 12)
end

-- <TILES>
-- </TILES>
-- <SPRITES>
-- </SPRITES>
-- <PALETTE>
-- 000:1a1c2c5d275db13e53ef7d57ffcd75a7f07038b76425717929366f3b5dc941a6f673eff7f4f4f494b0c2566c86333c57
-- </PALETTE>
```

### 9. Key Directives
1. Strictly modify the target file directly using your edit/write tools.
2. Maintain standard Lua 5.3 syntax and OpenTIC APIs.
3. Keep code compact, performant, and bug-free.
`;

function sendJson(res, statusCode, data) {
    const payload = JSON.stringify(data);
    res.writeHead(statusCode, {
        'Content-Type': 'application/json; charset=utf-8',
        'Content-Length': Buffer.byteLength(payload),
        'Access-Control-Allow-Origin': '*'
    });
    res.end(payload);
}

const server = http.createServer((req, res) => {
    // Handle CORS preflight
    if (req.method === 'OPTIONS') {
        res.writeHead(204, {
            'Access-Control-Allow-Origin': '*',
            'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
            'Access-Control-Allow-Headers': 'Content-Type'
        });
        res.end();
        return;
    }

    const url = new URL(req.url, `http://127.0.0.1:${PORT}`);

    if (req.method === 'GET' && url.pathname === '/health') {
        const hasOmp = fs.existsSync(OMP_EXE);
        sendJson(res, 200, {
            status: 'ok',
            omp_available: hasOmp,
            omp_path: OMP_EXE
        });
        return;
    }

    if (req.method === 'GET' && url.pathname === '/status') {
        if (!currentTask) {
            sendJson(res, 200, { status: 'idle' });
            return;
        }
        sendJson(res, 200, currentTask);
        return;
    }

    if (req.method === 'POST' && url.pathname === '/chat') {
        let body = '';
        req.on('data', chunk => body += chunk);
        req.on('end', () => {
            try {
                const data = JSON.parse(body);
                handleChatRequest(data, res);
            } catch (err) {
                sendJson(res, 400, { error: 'Invalid JSON body', details: err.message });
            }
        });
        return;
    }

    sendJson(res, 404, { error: 'Not found' });
});

function hasConfiguredModelOrKey() {
    const keyEnvVars = [
        'OPENAI_API_KEY', 'ANTHROPIC_API_KEY', 'GEMINI_API_KEY',
        'DEEPSEEK_API_KEY', 'GROQ_API_KEY', 'OPENROUTER_API_KEY',
        'ZAI_API_KEY', 'MINIMAX_API_KEY', 'AZURE_OPENAI_API_KEY',
        'XAI_API_KEY', 'MISTRAL_API_KEY', 'AWS_ACCESS_KEY_ID',
        'GITHUB_TOKEN', 'COPILOT_GITHUB_TOKEN'
    ];
    for (const k of keyEnvVars) {
        if (process.env[k] && process.env[k].trim().length > 0) {
            return true;
        }
    }
    const home = process.env.USERPROFILE || process.env.HOME || '';
    const modelsYml = path.join(home, '.omp', 'agent', 'models.yml');
    const settingsJson = path.join(home, '.omp', 'agent', 'settings.json');
    if (fs.existsSync(modelsYml) && fs.statSync(modelsYml).size > 10) return true;
    if (fs.existsSync(settingsJson) && fs.statSync(settingsJson).size > 10) return true;
    return false;
}

function handleChatRequest(data, res) {
    const { prompt, file_path, cart_code } = data;

    let targetFile = file_path;
    if (!targetFile || targetFile.trim() === '') {
        const home = process.env.USERPROFILE || process.env.HOME || '.';
        targetFile = path.join(home, 'AppData', 'Roaming', 'com.nesbox.tic', 'TIC-80', 'temp_game.lua');
    }
    targetFile = path.resolve(targetFile);

    // Ensure directory exists
    const dir = path.dirname(targetFile);
    if (!fs.existsSync(dir)) {
        fs.mkdirSync(dir, { recursive: true });
    }

    // Save current cart code to file if supplied and file doesn't exist or is older
    if (cart_code && cart_code.length > 0) {
        fs.writeFileSync(targetFile, cart_code, 'utf8');
    } else if (!fs.existsSync(targetFile)) {
        fs.writeFileSync(targetFile, '-- title:   New TIC-80 Game\n-- author:  TIC-80 AI\n-- script:  lua\n\nfunction TIC()\n    cls(0)\n    print("HELLO TIC-80!", 84, 64, 15)\nend\n', 'utf8');
    }

    const trimmedPrompt = (prompt || '').trim();

    // Check if it's a slash command
    if (trimmedPrompt.startsWith('/')) {
        const parts = trimmedPrompt.split(/\s+/);
        const cmd = parts[0].toLowerCase();

        if (cmd === '/key') {
            let provider = '';
            let key = '';
            if (parts.length === 2) {
                key = parts[1];
                if (key.startsWith('sk-ant')) provider = 'anthropic';
                else if (key.startsWith('AIzaSy')) provider = 'gemini';
                else if (key.startsWith('gsk_')) provider = 'groq';
                else provider = 'deepseek';
            } else if (parts.length >= 3) {
                provider = parts[1].toLowerCase();
                key = parts[2];
            } else {
                currentTask = {
                    status: 'done',
                    code_updated: false,
                    message: '【指令用法】/key <provider> <key>\n示例: /key deepseek sk-xxxx\n支持: deepseek, openai, gemini, anthropic, groq 等'
                };
                sendJson(res, 200, { status: 'started', targetFile });
                return;
            }

            const envVar = PROVIDER_MAP[provider] || `${provider.toUpperCase()}_API_KEY`;
            saveConfigKey(envVar, key);
            if (provider === 'deepseek' && (!activeModel || activeModel.includes('sk-'))) {
                saveActiveModel('deepseek-flash');
            }

            currentTask = {
                status: 'done',
                code_updated: false,
                message: `已成功配置并保存 ${provider.toUpperCase()} API Key！\n已存入环境变量与 ~/.tic_omp_keys.json，无需重启即可生效。`
            };
            sendJson(res, 200, { status: 'started', targetFile });
            return;
        }

        if (cmd === '/model') {
            if (parts.length >= 2) {
                if (parts[1].startsWith('sk-') || (parts[2] && parts[2].startsWith('sk-'))) {
                    let prov = parts[2] ? parts[1].toLowerCase() : 'deepseek';
                    let k = parts[2] ? parts[2] : parts[1];
                    const envVar = PROVIDER_MAP[prov] || `${prov.toUpperCase()}_API_KEY`;
                    saveConfigKey(envVar, k);
                    saveActiveModel(prov === 'deepseek' ? 'deepseek-flash' : '');
                    currentTask = {
                        status: 'done',
                        code_updated: false,
                        message: `已自动将此输入识别为 ${prov.toUpperCase()} API Key 并保存！\n已就绪。`
                    };
                    sendJson(res, 200, { status: 'started', targetFile });
                    return;
                }
                const rawModel = parts.slice(1).join(' ');
                const model = normalizeModelName(rawModel);
                saveActiveModel(model);
                currentTask = {
                    status: 'done',
                    code_updated: false,
                    message: `模型已成功切换为: ${model}\n后续修改将使用此模型。`
                };
            } else {
                currentTask = {
                    status: 'done',
                    code_updated: false,
                    message: `当前使用模型: ${activeModel || '默认自动'}\n【用法】/model <model_name>\n示例: /model deepseek-flash 或 /model gpt-4o`
                };
            }
            sendJson(res, 200, { status: 'started', targetFile });
            return;
        }

        if (cmd === '/status') {
            const configuredKeys = [];
            for (const [p, env] of Object.entries(PROVIDER_MAP)) {
                if (process.env[env] && !configuredKeys.some(k => k.env === env)) {
                    const val = process.env[env];
                    const masked = val.length > 8 ? `${val.slice(0, 4)}...${val.slice(-4)}` : '***';
                    configuredKeys.push(`${p} (${masked})`);
                }
            }
            const keyDesc = configuredKeys.length > 0 
                ? configuredKeys.join(', ')
                : '未配置 (请输入 /key <provider> <key> 进行设置)';

            currentTask = {
                status: 'done',
                code_updated: false,
                message: `【OpenTIC AI 系统状态】\n` +
                         `- Bridge 服务: 运行中 (http://127.0.0.1:${PORT})\n` +
                         `- 已就绪 Key: ${keyDesc}\n` +
                         `- 当前模型: ${activeModel || '默认自动'}\n` +
                         `- 目标卡带: ${path.basename(targetFile)}\n` +
                         `- 文件路径: ${targetFile}`
            };
            sendJson(res, 200, { status: 'started', targetFile });
            return;
        }

        if (cmd === '/undo') {
            const bakFile = `${targetFile}.bak`;
            if (fs.existsSync(bakFile)) {
                try {
                    const bakContent = fs.readFileSync(bakFile, 'utf8');
                    fs.writeFileSync(targetFile, bakContent, 'utf8');
                    currentTask = {
                        status: 'done',
                        code_updated: true,
                        new_code: bakContent,
                        message: `已成功撤销上次代码修改！代码已恢复至修改前状态并同步至卡带。`,
                        targetFile
                    };
                } catch (e) {
                    currentTask = {
                        status: 'error',
                        code_updated: false,
                        message: `撤销失败: ${e.message}`
                    };
                }
            } else {
                currentTask = {
                    status: 'done',
                    code_updated: false,
                    message: `未找到上一次代码的备份文件，无法撤销。`
                };
            }
            sendJson(res, 200, { status: 'started', targetFile });
            return;
        }
    }

    if (!hasConfiguredModelOrKey()) {
        console.warn('[Bridge] No API key detected');
        currentTask = {
            status: 'error',
            message: '未检测到 API Key！请在下方输入 /key <provider> <key>（如 /key deepseek sk-xxxx）进行配置，或输入 /help 查看帮助。',
            code_updated: false
        };
        sendJson(res, 200, {
            status: 'started',
            targetFile
        });
        return;
    }

    // Create backup before modifying
    try {
        if (fs.existsSync(targetFile)) {
            fs.copyFileSync(targetFile, `${targetFile}.bak`);
        }
    } catch (e) {
        console.warn('[Bridge] Failed to create backup:', e.message);
    }

    const beforeStats = fs.statSync(targetFile);
    const beforeContent = fs.readFileSync(targetFile, 'utf8');

    currentTask = {
        status: 'thinking',
        prompt,
        targetFile,
        startTime: Date.now(),
        summary: '',
        message: '',
        code_updated: false
    };

    sendJson(res, 200, {
        status: 'started',
        targetFile
    });

    executeAgent(targetFile, prompt, beforeStats, beforeContent);
}

function prepareCartContext(beforeContent, userPrompt) {
    if (!beforeContent || beforeContent.trim() === '') return '-- (Empty cartridge)';
    
    // Check if user request is asking about assets
    const lower = (userPrompt || '').toLowerCase();
    const wantsAssets = /(sprite|tile|map|palette|color|sfx|music|像素|精灵|图块|地图|画|颜色|素材|调色)/.test(lower);
    
    const assetIndex = beforeContent.search(/--\s*<(TILES|SPRITES|MAP|PALETTE|SFX|MUSIC|WAVES)>/i);
    if (assetIndex === -1) {
        return beforeContent;
    }
    
    const codePart = beforeContent.substring(0, assetIndex).trimEnd();
    const assetPart = beforeContent.substring(assetIndex);
    
    if (wantsAssets || assetPart.length < 1500) {
        return beforeContent;
    }
    
    const assetTags = [];
    const tagMatches = assetPart.match(/--\s*<([A-Z]+)>/gi);
    if (tagMatches) {
        tagMatches.forEach(t => assetTags.push(t.trim()));
    }
    
    return `${codePart}\n\n-- [Asset Section Notice: ${assetTags.join(', ')}]\n-- The cartridge contains trailing asset blocks at the bottom of the file.\n-- When editing code, preserve all existing asset blocks untouched.\n`;
}

function executeAgent(targetFile, userPrompt, beforeStats, beforeContent) {
    const fileName = path.basename(targetFile);
    const cartContext = prepareCartContext(beforeContent, userPrompt);
    const fullPrompt = `${TIC80_SYSTEM_PROMPT}

### Target File:
${targetFile}

### Current Cartridge Code (\`${fileName}\`):
\`\`\`lua
${cartContext}
\`\`\`

### User Request:
${userPrompt}
`;

    let ompProcess;
    const ompBin = fs.existsSync(OMP_EXE) ? OMP_EXE : 'omp';

    log(`[Bridge] Invoking agent for file: ${targetFile}`);
    log(`[Bridge] User prompt: ${userPrompt}`);
    let modelToUse = normalizeModelName(activeModel || (process.env.DEEPSEEK_API_KEY ? 'deepseek-flash' : ''));
    if (modelToUse) log(`[Bridge] Using model: ${modelToUse}`);

    try {
        const args = [
            '--auto-approve',
            '--allow-home',
            '--no-session',
            '--tools', 'read,edit,write',
            '--no-title',
            '--thinking=low'
        ];
        if (modelToUse) {
            args.push('--model', modelToUse);
        }
        args.push('-p', fullPrompt);
        log(`[Bridge] Spawning: ${ompBin}`, args);
        ompProcess = spawn(ompBin, args, {
            cwd: path.dirname(targetFile),
            windowsHide: true,
            env: process.env,
            stdio: ['ignore', 'pipe', 'pipe']
        });

        let stdoutData = '';
        let stderrData = '';
        let timedOut = false;

        const timer = setTimeout(() => {
            if (ompProcess && !ompProcess.killed) {
                timedOut = true;
                log('[Bridge] Agent timed out after 3600s');
                ompProcess.kill();
                currentTask = {
                    status: 'error',
                    message: 'Agent 执行超时 (60分钟)，请精简修改需求后重试。',
                    code_updated: false
                };
            }
        }, 3600000);

        ompProcess.stdout.on('data', chunk => {
            stdoutData += chunk.toString();
            currentTask.status = 'modifying';
        });

        ompProcess.stderr.on('data', chunk => {
            stderrData += chunk.toString();
        });

        ompProcess.on('close', code => {
            clearTimeout(timer);
            if (timedOut) return;

            log(`[Bridge] Agent finished with code: ${code}`);

            if (code !== 0) {
                let rawErr = (stderrData || '').replace(/Working\.\.\./g, '').trim();
                if (!rawErr) rawErr = (stdoutData || '').trim();
                log(`[Bridge] Error output: ${rawErr}`);
                let userErrMsg = rawErr;
                if (rawErr.includes('No API key found for yolo-auto') || rawErr.includes('yolo-auto')) {
                    log('[Bridge] Detected yolo-auto fallback, auto-repairing model to deepseek-flash');
                    saveActiveModel('deepseek-flash');
                    userErrMsg = '检测到模型配置不匹配，已自动重置为 deepseek-flash。请再次发送您的修改指令！';
                } else if (!hasConfiguredModelOrKey() && (rawErr.includes('No models available') || rawErr.includes('set an API key'))) {
                    userErrMsg = '未配置可用模型或 API Key！请输入 /key <provider> <key> 配置。';
                } else if (rawErr.includes('No API key found for')) {
                    userErrMsg = '当前所选模型未配置有效 API Key！请使用 /key 配置对应服务商，或输入 /model deepseek-flash 切换。';
                } else if (rawErr.includes('No models available')) {
                    userErrMsg = '当前无可用模型。请确认 API Key 是否有效，或输入 /key 重新设置。';
                } else if (userErrMsg.length > 200) {
                    userErrMsg = userErrMsg.substring(0, 200) + '...';
                }
                currentTask = {
                    status: 'error',
                    message: userErrMsg || `Agent 异常退出 (exit code: ${code})`,
                    code_updated: false
                };
                return;
            }

            let codeUpdated = false;
            let summary = '';
            let afterContent = '';

            try {
                if (fs.existsSync(targetFile)) {
                    afterContent = fs.readFileSync(targetFile, 'utf8');
                    if (afterContent !== beforeContent) {
                        codeUpdated = true;
                        const beforeLines = beforeContent.split('\n').length;
                        const afterLines = afterContent.split('\n').length;
                        summary = `代码已更新 (行数: ${beforeLines} -> ${afterLines})`;
                    }
                }
            } catch (e) {
                log('[Bridge] Error checking file update:', e);
            }

            const cleanMessage = stdoutData.trim() || (codeUpdated ? '已为您完成代码修改并更新至卡带。' : '执行完成。');

            currentTask = {
                status: 'done',
                code_updated: codeUpdated,
                summary: summary || (codeUpdated ? '代码已更新' : '未修改代码'),
                message: cleanMessage,
                new_code: codeUpdated ? afterContent : null,
                targetFile
            };
        });

        ompProcess.on('error', err => {
            clearTimeout(timer);
            log('[Bridge] Failed to launch agent:', err);
            currentTask = {
                status: 'error',
                message: `无法启动 oh-my-pi (${err.message})。请检查 omp.exe 是否就绪。`,
                code_updated: false
            };
        });

    } catch (e) {
        log('[Bridge] Execution error:', e);
        currentTask = {
            status: 'error',
            message: `执行出错: ${e.message}`,
            code_updated: false
        };
    }
}

server.listen(PORT, '127.0.0.1', () => {
    console.log(`OpenTIC <-> oh-my-pi Bridge running at http://127.0.0.1:${PORT}`);
});
