#pragma once
// Data log in RAM - the last 4 hours of the controller every 10 s, plus motor starts per hour
// for the last 48 hours. Served by the ESPHome web server:
//   /datalog      page with charts
//   /datalog.csv  the samples as CSV
//   /starts.csv   motor starts per hour as CSV
// The log lives in RAM and starts over after a restart.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <functional>
#include <string>
#include "esphome/components/web_server_base/web_server_base.h"

namespace datalog {

constexpr int SAMPLE_S = 10;                   // one sample per controller tick
constexpr int SAMPLES = 4 * 3600 / SAMPLE_S;   // 4 hours = 1440 samples (16 bytes each)
constexpr int HOURS = 48;

enum Flags : uint8_t {
  F_MANUAL = 1, F_DIRECT = 2, F_SENSOR_FAULT = 4, F_VALVE_FAULT = 8, F_CALIBRATING = 16,
};

struct Sample {
  uint32_t t;                  // uptime, s
  int16_t temp;                // °C x 100, INT16_MIN = no reading
  int16_t sp;                  // °C x 10
  int16_t pos, out, integ;     // % x 10
  uint8_t flags;
  uint8_t starts;              // motor starts since the previous sample
};

struct HourBucket {
  uint32_t t;                  // uptime at the start of the hour, s
  uint16_t starts;
};

class Log {
 public:
  std::function<time_t()> now_epoch;  // wall clock (0 = not known yet); set by the YAML

  void add(uint32_t uptime_s, float temp, float sp, float pos, float out, float integ,
           uint8_t flags, uint32_t total_starts) {
    uint32_t st = first_ ? 0 : total_starts - last_starts_;
    last_starts_ = total_starts;
    first_ = false;

    Sample &s = buf_[head_];
    s.t = uptime_s;
    s.temp = std::isnan(temp) ? INT16_MIN : (int16_t) lroundf(temp * 100.0f);
    s.sp = (int16_t) lroundf(sp * 10.0f);
    s.pos = (int16_t) lroundf(pos * 10.0f);
    s.out = (int16_t) lroundf(out * 10.0f);
    s.integ = (int16_t) lroundf(integ * 10.0f);
    s.flags = flags;
    s.starts = (uint8_t) std::min<uint32_t>(st, 255);
    head_ = (head_ + 1) % SAMPLES;
    if (count_ < SAMPLES) count_++;

    if (hour_count_ == 0 || uptime_s - hours_[hour_head_].t >= 3600) {
      if (hour_count_ > 0) hour_head_ = (hour_head_ + 1) % HOURS;
      hours_[hour_head_] = {uptime_s, 0};
      if (hour_count_ < HOURS) hour_count_++;
    }
    hours_[hour_head_].starts += st;
  }

  // Motor starts in the last n hour buckets, including the current one
  uint32_t starts_last_hours(int n) const {
    uint32_t sum = 0;
    for (int i = 0; i < std::min(n, hour_count_); i++)
      sum += hours_[(hour_head_ - i + HOURS) % HOURS].starts;
    return sum;
  }

  int count() const { return count_; }
  const Sample &sample(int i) const { return buf_[(head_ - count_ + i + SAMPLES) % SAMPLES]; }  // 0 = oldest
  int hour_count() const { return hour_count_; }
  const HourBucket &hour(int i) const { return hours_[(hour_head_ - hour_count_ + 1 + i + HOURS) % HOURS]; }

  // "yyyy-MM-dd HH:mm:ss" for a sample taken at uptime t, or "+<uptime>s" without a clock
  void format_time(uint32_t t, uint32_t uptime_now, char *buf, size_t n) const {
    time_t now = now_epoch ? now_epoch() : 0;
    if (now > 1600000000) {
      time_t ts = now - (time_t) (uptime_now - t);
      struct tm tm;
      localtime_r(&ts, &tm);
      strftime(buf, n, "%Y-%m-%d %H:%M:%S", &tm);
    } else {
      snprintf(buf, n, "+%us", (unsigned) t);
    }
  }

 private:
  Sample buf_[SAMPLES] = {};
  int head_ = 0, count_ = 0;
  HourBucket hours_[HOURS] = {};
  int hour_head_ = 0, hour_count_ = 0;
  uint32_t last_starts_ = 0;
  bool first_ = true;
};

inline Log dlog;

// Chart page; fetches the CSV files and draws them on canvases (no external libraries)
static const char PAGE[] = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Valve controller log</title>
<style>body{font-family:sans-serif;margin:16px;max-width:1100px}canvas{width:100%;height:260px;border:1px solid #ccc;margin-bottom:6px}
h2{font-size:16px;margin:18px 0 4px}.k{display:inline-block;width:12px;height:3px;margin:0 4px 3px 12px}p{margin:4px 0;font-size:13px;color:#555}</style></head>
<body><h1 style="font-size:20px">Valve controller - last 4 hours</h1>
<p><a href="/">Back</a> · <a href="/datalog.csv">datalog.csv</a> · <a href="/starts.csv">starts.csv</a> · <span id="info"></span></p>
<h2>Temperature<span class="k" style="background:#e8590c"></span>return<span class="k" style="background:#888"></span>setpoint</h2><canvas id="c1"></canvas>
<h2>Valve<span class="k" style="background:#1971c2"></span>position<span class="k" style="background:#2f9e44"></span>target<span class="k" style="background:#ae3ec9"></span>I term</h2><canvas id="c2"></canvas>
<h2>Motor starts per 10 min</h2><canvas id="c3"></canvas>
<h2>Motor starts per hour (48 h)</h2><canvas id="c4"></canvas>
<script>
function csv(t){return t.trim().split('\n').slice(1).map(l=>l.split(','))}
function draw(id,xs,series,opt){const c=document.getElementById(id),r=devicePixelRatio||1;c.width=c.clientWidth*r;c.height=c.clientHeight*r;
const g=c.getContext('2d');g.scale(r,r);const W=c.clientWidth,H=c.clientHeight,L=44,R=8,T=8,B=22;
let lo=opt.min,hi=opt.max;if(lo===undefined){lo=1e9;hi=-1e9;series.forEach(s=>s.v.forEach(v=>{if(v!==null&&!isNaN(v)){lo=Math.min(lo,v);hi=Math.max(hi,v)}}));
const p=Math.max(0.5,(hi-lo)*0.1);lo=Math.floor(lo-p);hi=Math.ceil(hi+p)}
const x0=xs[0],x1=xs[xs.length-1]||x0+1,X=x=>L+(x-x0)/Math.max(1,x1-x0)*(W-L-R),Y=v=>T+(hi-v)/(hi-lo)*(H-T-B);
g.font='11px sans-serif';g.fillStyle='#555';g.strokeStyle='#eee';
for(let i=0;i<=5;i++){const v=lo+(hi-lo)*i/5,y=Y(v);g.beginPath();g.moveTo(L,y);g.lineTo(W-R,y);g.stroke();g.fillText(v.toFixed(hi-lo<10?1:0),2,y+4)}
const n=Math.min(8,xs.length);for(let i=0;i<n;i++){const k=Math.round(i*(xs.length-1)/Math.max(1,n-1)),lab=opt.lab[k]||'';g.fillText(lab.slice(-8,-3),X(xs[k])-14,H-6)}
series.forEach(s=>{g.strokeStyle=s.c;g.fillStyle=s.c;g.lineWidth=1.5;
if(s.bar){const w=Math.max(2,(W-L-R)/xs.length*0.8);xs.forEach((x,i)=>{if(s.v[i])g.fillRect(X(x)-w/2,Y(s.v[i]),w,Y(lo)-Y(s.v[i]))});return}
g.beginPath();let up=false;xs.forEach((x,i)=>{const v=s.v[i];if(v===null||isNaN(v)){up=false;return}up?g.lineTo(X(x),Y(v)):g.moveTo(X(x),Y(v));up=true});g.stroke()})}
Promise.all([fetch('/datalog.csv').then(r=>r.text()),fetch('/starts.csv').then(r=>r.text())]).then(([a,b])=>{
const d=csv(a),h=csv(b);if(!d.length){document.getElementById('info').textContent='no data yet';return}
const xs=d.map(r=>+r[1]),lab=d.map(r=>r[0]),f=i=>d.map(r=>r[i]===''?null:+r[i]);
draw('c1',xs,[{v:f(3),c:'#888'},{v:f(2),c:'#e8590c'}],{lab});
draw('c2',xs,[{v:f(6),c:'#ae3ec9'},{v:f(5),c:'#2f9e44'},{v:f(4),c:'#1971c2'}],{min:0,max:100,lab});
const bx=[],bv=[],bl=[];for(let i=0;i<d.length;i+=60){let s=0;for(let j=i;j<Math.min(i+60,d.length);j++)s+=+d[j][8];bx.push(xs[i]);bv.push(s);bl.push(lab[i])}
draw('c3',bx,[{v:bv,c:'#1971c2',bar:1}],{min:0,max:Math.max(5,...bv),lab:bl});
draw('c4',h.map(r=>+r[1]),[{v:h.map(r=>+r[2]),c:'#1971c2',bar:1}],{min:0,max:Math.max(5,...h.map(r=>+r[2])),lab:h.map(r=>r[0])});
const tot=d.reduce((s,r)=>s+ +r[8],0);document.getElementById('info').textContent=d.length+' samples, '+tot+' motor starts in this period'})
</script></body></html>)HTML";

class Handler : public esphome::web_server_idf::AsyncWebHandler {
 public:
  bool canHandle(esphome::web_server_idf::AsyncWebServerRequest *request) const override {
    if (request->method() != HTTP_GET) return false;
    char buf[esphome::web_server_idf::AsyncWebServerRequest::URL_BUF_SIZE];
    request->url_to(buf);
    return !strcmp(buf, "/datalog") || !strcmp(buf, "/datalog.csv") || !strcmp(buf, "/starts.csv");
  }

  void handleRequest(esphome::web_server_idf::AsyncWebServerRequest *request) override {
    char ubuf[esphome::web_server_idf::AsyncWebServerRequest::URL_BUF_SIZE];
    request->url_to(ubuf);
    const char *url = ubuf;
    if (!strcmp(url, "/datalog")) {
      request->send(request->beginResponse(200, "text/html", (const uint8_t *) PAGE, sizeof(PAGE) - 1));
      return;
    }
    // CSV is sent in chunks; the whole log would not fit in one buffer
    request->beginResponse(200, "text/csv");
    httpd_req_t *req = *request;
    uint32_t now = esphome::millis() / 1000;
    std::string out;
    out.reserve(1500);
    char line[96], ts[24];
    auto flush = [&](bool force) {
      if (out.size() > 1200 || (force && !out.empty())) {
        httpd_resp_send_chunk(req, out.data(), out.size());
        out.clear();
      }
    };
    if (!strcmp(url, "/datalog.csv")) {
      out = "time,uptime_s,return_c,setpoint_c,position_pct,target_pct,i_term_pct,flags,starts\n";
      for (int i = 0; i < dlog.count(); i++) {
        const Sample &s = dlog.sample(i);
        dlog.format_time(s.t, now, ts, sizeof ts);
        char temp[12] = "";
        if (s.temp != INT16_MIN) snprintf(temp, sizeof temp, "%.2f", s.temp / 100.0f);
        snprintf(line, sizeof line, "%s,%u,%s,%.1f,%.1f,%.1f,%.1f,%u,%u\n", ts, (unsigned) s.t, temp,
                 s.sp / 10.0f, s.pos / 10.0f, s.out / 10.0f, s.integ / 10.0f, s.flags, s.starts);
        out += line;
        flush(false);
      }
    } else {
      out = "hour_start,uptime_s,starts\n";
      for (int i = 0; i < dlog.hour_count(); i++) {
        const HourBucket &h = dlog.hour(i);
        dlog.format_time(h.t, now, ts, sizeof ts);
        snprintf(line, sizeof line, "%s,%u,%u\n", ts, (unsigned) h.t, (unsigned) h.starts);
        out += line;
      }
    }
    flush(true);
    httpd_resp_send_chunk(req, nullptr, 0);
  }
};

}  // namespace datalog
