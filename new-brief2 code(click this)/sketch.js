/* ---------------- SETTINGS ---------------- */
const BAUD = 115200;
const WINDOW_SEC = 10;
const INTERVAL_COUNT = 6;
const MISMATCH_THRESHOLD = 15;
const MIN_BEAT_GAP_MS = 250;
const ECG_BUFFER_LENGTH = 800;

/* GSR mapping */
const MAP_MIN = 230;
const MAP_MAX = 240;
let SMOOTHING = 0.6;
let RISE_DECAY = 0.02;

/* -------------- SERIAL STATE --------------- */
let port = null, reader = null, decoder = null;

let arduinoBPM = null;
let beatState = 0;
let lastRecordedBeatTime = 0;

/* ECG buffer */
let ecgBuffer = new Array(ECG_BUFFER_LENGTH).fill(512);
 

/* timestamps */
let beatTimestamps = [];
let intervals = [];

let estBPM_window = null;
let estBPM_interval = null;
let mismatch = false;

/* GSR */
let latestValue2 = null;
let latestContact = 0;
let riseStrength = 0;
let targetSmooth = 0;

/* visual */
let jump = 0;
let targetJump = 0;
let lastBeatVisual = 0;
let sizeSmooth = 1.0;

/* DOM */
const btn = document.getElementById('connect');
const info = document.getElementById('info');
const alertBox = document.getElementById('alert');
btn.onclick = connectSerial;

/* ---------------- Serial ---------------- */
async function connectSerial(){
  if (!('serial' in navigator)) {
    alert('Use Chrome / Edge via HTTPS or localhost');
    return;
  }
  port = await navigator.serial.requestPort();
  await port.open({ baudRate: BAUD });

  btn.textContent = 'Connected';
  btn.disabled = true;

  const tds = new TextDecoderStream();
  port.readable.pipeTo(tds.writable);
  reader = tds.readable.getReader();

  readLoop();
}

async function readLoop(){
  let buffer = '';
  while (true) {
    const { value, done } = await reader.read();
    if (done) break;
    buffer += value;
    const lines = buffer.split(/\r?\n/);
    buffer = lines.pop();
    lines.forEach(l => parseLine(l.trim()));
  }
}

/* ---------------- Parser ---------------- */
function parseLine(line){
  if (!line) return;
  const now = Date.now();

  let m;
  if (m = line.match(/RAW\s+(\d+)/i)) pushECG(+m[1]);
  if (m = line.match(/BPM\s+(-?\d+)/i)) arduinoBPM = +m[1];

  if (m = line.match(/Beat\s+([01])/i)) {
    const b = +m[1];
    if (b === 1 && beatState === 0 && now - lastRecordedBeatTime > MIN_BEAT_GAP_MS) {
      beatTimestamps.push(now);
      if (beatTimestamps.length > 1) {
        intervals.push(
          beatTimestamps.at(-1) - beatTimestamps.at(-2)
        );
        if (intervals.length > INTERVAL_COUNT) intervals.shift();
      }
      lastRecordedBeatTime = now;
      lastBeatVisual = now;
    }
    beatState = b;
    computeEstimates();
  }

  if (m = line.match(/Value2\s+(-?\d+)/i)) latestValue2 = +m[1];
  if (m = line.match(/Contact\s+([01])/i)) latestContact = +m[1];
}

/* ---------------- Estimation ---------------- */
function pushECG(v){
  ecgBuffer.push(v);
  if (ecgBuffer.length > ECG_BUFFER_LENGTH) ecgBuffer.shift();
}

function computeEstimates(){
  const now = Date.now();
  beatTimestamps = beatTimestamps.filter(t => t > now - WINDOW_SEC*1000);

  estBPM_window = beatTimestamps.length
    ? beatTimestamps.length / WINDOW_SEC * 60
    : null;

  if (intervals.length) {
    const avg = intervals.reduce((a,b)=>a+b)/intervals.length;
    estBPM_interval = 60000/avg;
  } else estBPM_interval = null;

  mismatch =
    arduinoBPM &&
    estBPM_window &&
    Math.abs(arduinoBPM - estBPM_window) > MISMATCH_THRESHOLD;
}

/* ---------------- p5 ---------------- */
let ecgTop, ecgBottom, ecgCanvasHeight;

function setup(){
  createCanvas(windowWidth, windowHeight);
  frameRate(60);
  resizeECG();
}

function draw(){
  background(10);
  drawECGBackground();

  const gsrTarget = latestContact ? mapGSR() : 0;
  riseStrength += (gsrTarget - riseStrength) * SMOOTHING;
  if (!latestContact) riseStrength = max(0, riseStrength - RISE_DECAY);
  targetSmooth += (riseStrength - targetSmooth) * 0.2;

  const bpm = estBPM_interval || estBPM_window || arduinoBPM || 60;
  const now = Date.now();
  const dt = now - lastBeatVisual;

  if (dt < 450) targetJump = max(0.9 - dt/450, 0.1);
  else {
    const p = 60000/bpm;
    const t = (millis()%p)/p;
    targetJump = 0.04 + 0.18*(0.5 - abs(t-0.5))*2;
  }

  jump += (targetJump - jump) * map(bpm,40,180,0.12,0.6);

  const y = height/2 - min(width,height)*0.1*jump;
  sizeSmooth += ((1 + targetSmooth*0.28 + jump*0.06) - sizeSmooth)*0.08;

  fill(
  lerp(255, 30, targetSmooth),  // R：浅黄色→蓝
  lerp(245, 150, targetSmooth), // G：浅黄色→蓝
  lerp(200, 255, targetSmooth)  // B：浅黄色→蓝
);

  ellipse(width/2, y, min(width,height)*0.36*sizeSmooth);

  drawECGWave();

  info.innerText =
    `BPM:${arduinoBPM ?? '—'}  win:${estBPM_window?.toFixed(1) ?? '—'}  int:${estBPM_interval?.toFixed(1) ?? '—'}  GSR:${latestValue2 ?? '—'}`;

  alertBox.style.display = mismatch ? 'block' : 'none';
}

function drawECGBackground(){
  fill(18);
  rect(12, ecgTop-10, width-24, ecgCanvasHeight+20, 8);
}

function drawECGWave(){
  stroke(0,200,255);
  strokeWeight(4);
  noFill();
  beginShape();
  ecgBuffer.forEach((v,i)=>{
    const x = map(i,0,ecgBuffer.length-1,20,width-20);
    const y = (ecgTop+ecgBottom)/2 - (v-512)/512*(ecgCanvasHeight/3.4);
    vertex(x,y);
  });
  endShape();
  noStroke();
}

function mapGSR(){
  return constrain((latestValue2-MAP_MIN)/(MAP_MAX-MAP_MIN),0,1);
}

function resizeECG(){
  ecgCanvasHeight = height*0.28;
  ecgTop = (height-ecgCanvasHeight)/2;
  ecgBottom = ecgTop + ecgCanvasHeight;
}

function windowResized(){
  resizeCanvas(windowWidth,windowHeight);
  resizeECG();
}
