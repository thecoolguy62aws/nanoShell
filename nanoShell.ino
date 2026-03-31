#include <WiFi.h>
#include <WebServer.h>

const char* ap_ssid = "nano-shell";
const char* ap_password = "12345678";

WebServer server(80);

// HTML terminal
const char TERMINAL_HTML[] PROGMEM = R"=====(<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
<title>nano-shell</title>
<style>
* { box-sizing: border-box; margin: 0; padding: 0; }
body, html { 
    height: 100%; width: 100%; 
    background: #0d0d0d; color: #c5c5c5; 
    font-family: 'Ubuntu Mono', 'Courier New', monospace; 
    overflow: hidden;
}
#console-container {
    display: flex; flex-direction: column;
    height: 100vh; padding: 15px;
}
#output {
    flex-grow: 1; overflow-y: auto; margin-bottom: 5px;
    white-space: pre-wrap; word-wrap: break-word; line-height: 1.4;
}
.input-area { display: flex; align-items: center; }
.user { color: /*#50fa7b*//*#24bf4c*/#29c451; white-space: nowrap; font-weight: bold; }
.dir { color: #8be9fd; }
.seperator { color: #f8f8f2; font-weight: bold; }
.final-prompt { color: #f8f8f2; margin-right: 4px; }
.final-prompt-history { color: #f8f8f2; }
.history-command { color: #f8f8f2; }

input { 
    background: transparent; border: none; color: #f8f8f2; 
    outline: none; width: 100%; font-family: inherit; font-size: 16px;
}
/* CRT scanline effect */
body::after {
    content: " "; display: block; position: fixed; top: 0; left: 0; bottom: 0; right: 0;
    background: linear-gradient(rgba(18, 16, 16, 0) 50%, rgba(0, 0, 0, 0.1) 50%);
    z-index: 2; background-size: 100% 2px; pointer-events: none;
}
</style>
</head>
<body onclick="document.getElementById('cmdInput').focus()">
<div id="console-container">
    <div id="output">Welcome to nano-shell v0.0.0<br>Type 'help' for commands.<br><br></div>
    <div class="input-area">
        <span class="user">root@nano</span><span class="seperator">:</span><span class="dir">~</span><span class="final-prompt">#</span>
        <input type="text" id="cmdInput" autocomplete="off" autofocus onkeydown="if(event.key==='Enter') sendCommand()">
    </div>
</div>
<script>
function sendCommand() {
    const input = document.getElementById('cmdInput');
    const output = document.getElementById('output');
    const cmd = input.value.trim();
    if(!cmd) return;
    output.innerHTML += `<div><span class="user">root@nano</span><span class="seperator">:</span><span class="dir">~</span><span class="final-prompt-history">#</span> <span class="history-command">${cmd}</span></div>`;
    input.value = '';

    if(cmd=="clear"){ output.innerHTML=""; return; }

    fetch(`/cmd?text=${encodeURIComponent(cmd)}`)
        .then(r=>r.text())
        .then(data=>{
            output.innerHTML += `<div>${data}</div>`;
            output.scrollTop = output.scrollHeight;
        });
}
</script>
</body>
</html>
)=====";

// Command structure
#include <map>
#include <functional>
std::map<String, std::function<String(const String&)>> commands;

void setupCommands() {
    // GPIO command
    commands["gpio"] = [](const String &args) -> String {
        String resp;

        if(args.startsWith("write")) {
            int pin; char state[6];
            if(sscanf(args.c_str(), "write %d %5s", &pin, state)==2) {
                pinMode(pin, OUTPUT);
                String st(state);
                st.toUpperCase(); 
                if(st=="HIGH") digitalWrite(pin,HIGH);
                else if(st=="LOW") digitalWrite(pin,LOW);
                else return "Error: invalid state, use high/low";
                resp = "Pin " + String(pin) + " set to " + st;
            } else resp="Error: incorrect syntax, usage: gpio write [pin] [high/low]";
        }
        else if(args.startsWith("read")) {
            int pin;
            if(sscanf(args.c_str(),"read %d",&pin)==1){
                pinMode(pin,INPUT);
                int val=digitalRead(pin);
                resp = "Pin " + String(pin) + " reads " + String(val);
            } else resp="Error: incorrect syntax, usage: gpio read [pin]";
        }
        else if(args.startsWith("mode")) {
            int pin; char mode[10];
            if(sscanf(args.c_str(),"mode %d %9s",&pin,mode)==2){
                String m(mode);
                m.toLowerCase();
                if(m=="input") pinMode(pin,INPUT);
                else if(m=="output") pinMode(pin,OUTPUT);
                else return "Error: invalid mode, use input/output";
                resp = "Pin " + String(pin) + " mode set to " + m;
            } else resp="Error: incorrect syntax, usage: gpio mode [pin] [input/output]";
        }
        else if(args.startsWith("pwm")) {
            int pin,duty;
            if(sscanf(args.c_str(),"pwm %d %d",&pin,&duty)==2){
                if(duty<0) duty=0;
                if(duty>255) duty=255;
                ledcAttachPin(pin,0);
                ledcSetup(0,5000,8);
                ledcWrite(0,duty);
                resp = "PWM on pin " + String(pin) + " set to duty " + String(duty);
            } else resp="error: incorrect syntax, usage: gpio pwm [pin] [0-255]";
        }
        else if(args=="-h"||args=="--help") {
            resp = "gpio commands:\n- write [pin] [high/low]\n- read [pin]\n- mode [pin] [input/output]\n- pwm [pin] [0-255]";
        }
        else resp="error: unknown gpio subcommand";
        return resp;
    };

    // Uptime command
    commands["uptime"] = [](const String&) -> String {
        long sec = millis()/1000;
        return "up " + String(sec/60) + " min, " + String(sec%60) + " sec";
    };

    commands["reboot"] = [](const String&) -> String {
        ESP.restart();
        return "rebooted";
    };

    // Help command
    commands["help"] = [](const String&) -> String {
        return "Available commands: help, clear, uptime, gpio -h, reboot";
    };
}

// Handle commands
void handleCommand() {
    if(!server.hasArg("text")) { server.send(200,"text/plain",""); return; }
    String full = server.arg("text");
    full.trim();

    if(full=="clear") { server.send(200,"text/plain",""); return; }

    // Split command and arguments
    String cmd, args;
    int sp = full.indexOf(' ');
    if(sp!=-1) { cmd = full.substring(0,sp); args = full.substring(sp+1); }
    else cmd = full;

    String resp;
    if(commands.count(cmd)) resp = commands[cmd](args);  // existing command
    else resp = cmd + ": command not found";

    server.send(200,"text/plain",resp);
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_BUILTIN,OUTPUT);

    WiFi.mode(WIFI_AP);
    WiFi.softAP(ap_ssid,ap_password, 1, 0, 4);

    server.on("/", []() { server.send(200,"text/html",TERMINAL_HTML); });
    server.on("/cmd", handleCommand);
    server.begin();

    setupCommands();
    Serial.println("Nano-shell ready. Connect to WiFi SSID: nano-shell");
}

void loop() {
    server.handleClient();
}
