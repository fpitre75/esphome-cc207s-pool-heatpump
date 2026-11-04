/**
 * @file hwp_web_dashboard.cpp
 * @brief Firmware-served HWP diagnostic dashboard support.
 *
 * Copyright (c) 2024 S. Leclerc (sle118@hotmail.com)
 *
 * This file is part of the Pool Heater Controller component project.
 *
 * @project Pool Heater Controller Component
 * @developer S. Leclerc (sle118@hotmail.com)
 *
 * @license MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies or substantial portions of the Software.
 */

#include "hwp_web_dashboard.h"

#include "hwp_time_adapter.h"
#include "hwp_version.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <map>
#include <utility>

#ifndef HWP_NATIVE_TEST
#include "esphome/core/hal.h"
#endif

namespace esphome {
namespace hwp {

namespace {

static constexpr size_t HWP_WEB_STATE_PACKET_LIMIT = 12;
static constexpr uint32_t HWP_WEB_REPEAT_COLLAPSE_MS = 1500;

const char HWP_WEB_INDEX[] =
    "<!doctype html><html><head><meta charset=utf-8><meta name=viewport "
    "content='width=device-width,initial-scale=1'><title>HWP</title><style>"
    "body{margin:0 0 76px;font:14px system-ui;background:#f5f7fa;color:#17202a}"
    "header{padding:12px 16px;background:#17324d;color:white;display:flex;gap:12px;align-items:center}"
    "button{border:0;background:#dce6f1;padding:8px 10px;margin:0 4px 0 0;border-radius:4px}"
    "input{padding:8px;border:1px solid #c5d0dc;border-radius:4px;min-width:220px}"
    "button.active{background:#1d6fb8;color:white}.view{display:none;padding:10px}.view.active{display:block}"
    "table{border-collapse:collapse;width:100%;background:white}th,td{padding:6px 8px;border-bottom:1px solid #e1e5ea;text-align:left}"
    "tr.changed,span.changed{background:#fff2a8}.bad{color:#a40000;font-weight:700}.packet{font-family:ui-monospace,monospace;margin:6px 0;padding:8px;background:white;border-left:4px solid #8aa4bd;overflow:auto}"
    ".annotate{position:fixed;left:0;right:0;bottom:0;z-index:20;padding:10px;background:#edf3f8;border-top:1px solid #c6d4e1;box-shadow:0 -2px 8px #0002}"
    ".meta{font-size:12px;color:#52616f}.chart{background:white;border:1px solid #d8dee6;margin:10px 0;padding:8px}.chart h3{margin:0 0 4px;font-size:15px;color:#17324d}.chart p{margin:0 0 6px}.chart canvas{display:block;width:100%;height:260px}"
    "</style></head><body><header><strong>HWP</strong><span id=meta></span></header>"
    "<nav style='padding:8px;background:#e9eef4'><button data-tab=values class=active>Values</button><button data-tab=frames>Frames</button><button data-tab=packets>Packets</button><button data-tab=graphs>Graphs</button></nav>"
    "<section id=values class='view active'><table><thead><tr><th>Field</th><th>Value</th><th>Age</th><th>Source</th><th>Frame</th><th>Raw</th></tr></thead><tbody id=valueRows></tbody></table></section>"
    "<section id=frames class=view><div class=meta>Latest packet seen for each frame/source/length, including unknown frames.</div><div id=frameRows></div></section>"
    "<section id=packets class=view><div id=packetRows></div></section>"
    "<section id=graphs class=view><div class=meta>Recent graph history is retained on the device and loaded with graphs.json, so the chart starts with the latest sliding window.</div>"
    "<div class=chart><h3>Temperatures</h3><p class=meta id=g1m></p><canvas id=g1></canvas></div>"
    "<div class=chart><h3>Setpoints And Limits</h3><p class=meta id=g2m></p><canvas id=g2></canvas></div>"
    "<div class=chart><h3>Differentials And Timers</h3><p class=meta id=g3m></p><canvas id=g3></canvas></div>"
    "<div class=chart><h3>Fan Values</h3><p class=meta id=g4m></p><canvas id=g4></canvas></div></section>"
    "<section class=annotate><strong>Annotate</strong> <input id=annNote placeholder='label or note'><button id=annStart>Start</button><button id=annEvent>Mark Event</button><button id=annEnd>End</button><button id=annExport>Export JSON</button><button id=annClear>Clear</button><span id=annCount class=meta></span></section>"
    "<script>"
    "const ANN_KEY='hwp.web.annotations.v1';let state={fields:[],frames:[],packets:[],graphs:{},meta:{}};let changed={};let tab='values';let refreshPending=false;"
    "function uptime(){let base=state.meta.uptime_ms||0,local=state.local_sync_ms||Date.now();return base+Math.max(0,Date.now()-local)}"
    "function anns(){try{return JSON.parse(localStorage.getItem(ANN_KEY)||'[]')}catch(e){return[]}}"
    "function saveAnns(a){localStorage.setItem(ANN_KEY,JSON.stringify(a));annCount.textContent=a.length+' annotations'}"
    "function latestPacket(){return state.packets.length?state.packets[state.packets.length-1]:null}"
    "function compact(){let p=latestPacket();return{local_time:new Date().toISOString(),uptime_ms:state.meta.uptime_ms||0,note:annNote.value,tab:tab,packet:p?{sequence:p.sequence,kind:p.kind,frame:p.frame,label:p.label}:null,changed_fields:state.fields.filter(f=>f.changed).map(f=>f.id),snapshot:{meta:state.meta,fields:state.fields,packets:state.packets.slice(-5)}}}"
    "function addAnn(type){let a=anns();a.push(Object.assign({type:type},compact()));saveAnns(a)}"
    "document.querySelectorAll('button[data-tab]').forEach(b=>b.onclick=()=>{document.querySelectorAll('button[data-tab]').forEach(x=>x.classList.remove('active'));document.querySelectorAll('.view').forEach(x=>x.classList.remove('active'));b.classList.add('active');tab=b.dataset.tab;document.getElementById(tab).classList.add('active');render()});"
    "annStart.onclick=()=>addAnn('start');annEvent.onclick=()=>addAnn('event');annEnd.onclick=()=>addAnn('end');annClear.onclick=()=>{if(confirm('Clear local HWP annotations?'))saveAnns([])};"
    "annExport.onclick=()=>{let data=JSON.stringify({schema:'hwp-web-annotations-v1',exported_at:new Date().toISOString(),annotations:anns()},null,2);let u=URL.createObjectURL(new Blob([data],{type:'application/json'}));let a=document.createElement('a');a.href=u;a.download='hwp-web-annotations.json';a.click();URL.revokeObjectURL(u)};"
    "function age(ms){if(!ms)return'';let base=uptime();let s=Math.max(0,Math.floor((base-ms)/1000));return s<60?s+'s':Math.floor(s/60)+'m '+String(s%60).padStart(2,'0')+'s'}"
    "function packetHtml(p){return`<div class=packet><div class=meta>#${p.sequence} ${age(p.seen_ms)} ${p.kind} ${p.frame} ${p.label} ${p.source} ${p.length}b ${p.checksum_valid?'ok':'<span class=bad>bad checksum</span>'}</div>[${p.bytes.map((b,i)=>`<span class='${p.changed_bytes[i]?'changed':''}'>${b.toString(16).padStart(2,'0').toUpperCase()}</span>`).join(' ')}]</div>`}"
    "function render(){document.getElementById('meta').textContent=`${state.meta.status||''} ${state.meta.bus_mode||''} ${state.meta.revision||''}`;let now=Date.now();valueRows.innerHTML=state.fields.map(f=>`<tr class='${changed[f.id]>now?'changed':''}'><td>${f.label}</td><td>${f.value} ${f.unit||''}</td><td>${age(f.seen_ms)}</td><td>${f.source}</td><td>${f.frame}</td><td>${f.raw}</td></tr>`).join('');frameRows.innerHTML=(state.frames||[]).slice().reverse().map(packetHtml).join('');packetRows.innerHTML=state.packets.slice().reverse().map(packetHtml).join('');drawGraphs()}"
    "function fmeta(){let m={};state.fields.forEach(f=>m[f.id]=f);return m}"
    "function resizeCanvas(c){let r=c.getBoundingClientRect(),d=window.devicePixelRatio||1,w=Math.max(320,Math.floor(r.width)),h=Math.max(220,Math.floor(r.height));if(c.width!==Math.floor(w*d)||c.height!==Math.floor(h*d)){c.width=Math.floor(w*d);c.height=Math.floor(h*d)}let x=c.getContext('2d');x.setTransform(d,0,0,d,0,0);return{x,w,h}}"
    "function tick(ms){let s=Math.max(0,Math.round(ms/1000));return s<60?s+'s':Math.floor(s/60)+'m '+String(s%60).padStart(2,'0')+'s'}"
    "function drawOne(id,names){let c=document.getElementById(id),{x,w,h}=resizeCanvas(c),m=fmeta();x.clearRect(0,0,w,h);let now=uptime();let series=names.map(n=>({id:n,label:(m[n]&&m[n].label)||n,unit:(m[n]&&m[n].unit)||'',p:(state.graphs[n]||[]).slice()})).filter(s=>s.p.length);let note=document.getElementById(id+'m');if(!series.length){note.textContent='Waiting for retained samples from the device.';return}series.forEach(s=>{let last=s.p[s.p.length-1];if(last&&last.t<now)s.p.push({sequence:last.sequence,t:now,value:last.value,hold:true})});let vals=series.flatMap(s=>s.p.map(p=>p.value));let times=series.flatMap(s=>s.p.map(p=>p.t||p.sequence||0));let t1=Math.max(now,...times),oldest=Math.min(...times),spanMs=Math.max(60000,t1-oldest),t0=t1-spanMs;let lo=Math.min(...vals),hi=Math.max(...vals);if(lo===hi){lo-=1;hi+=1}let pad=(hi-lo)*0.08;lo-=pad;hi+=pad;let l=48,r=Math.max(120,Math.min(260,w*0.32)),top=18,b=h-44,gw=w-l-r,gh=b-top;let colors=['#1d6fb8','#c45f1a','#2e7d32','#7b1fa2','#a00030','#00838f','#6d4c41','#455a64'];x.strokeStyle='#dce3eb';x.lineWidth=1;x.fillStyle='#5b6773';x.font='12px system-ui';for(let i=0;i<=4;i++){let y=top+gh*i/4;x.beginPath();x.moveTo(l,y);x.lineTo(l+gw,y);x.stroke();let v=hi-(hi-lo)*i/4;x.fillText(v.toFixed(1),6,y+4)}for(let i=0;i<=4;i++){let px=l+gw*i/4;x.beginPath();x.moveTo(px,top);x.lineTo(px,b);x.stroke();let rem=t1-(t0+spanMs*i/4);let label=i===4?'now':'-'+tick(rem);x.fillText(label,px-14,b+18)}x.strokeStyle='#93a4b5';x.strokeRect(l,top,gw,gh);series.forEach((s,si)=>{x.strokeStyle=colors[si%colors.length];x.fillStyle=x.strokeStyle;x.lineWidth=2;x.beginPath();let drew=false;s.p.forEach(p=>{let tt=p.t||p.sequence||0;if(tt<t0)return;let px=l+(tt-t0)*gw/(t1-t0),py=top+gh-(p.value-lo)*gh/(hi-lo);if(drew)x.lineTo(px,py);else{x.moveTo(px,py);drew=true}});if(drew)x.stroke();s.p.filter(p=>!p.hold&&p.t>=t0).forEach(p=>{let px=l+(p.t-t0)*gw/(t1-t0),py=top+gh-(p.value-lo)*gh/(hi-lo);x.beginPath();x.arc(px,py,3,0,Math.PI*2);x.fill()});let last=s.p[s.p.length-1];let y=24+si*18;x.fillRect(w-r+8,y-8,9,9);x.fillText(`${s.label}: ${last.value.toFixed(1)}${s.unit?' '+s.unit:''}`,w-r+22,y)});note.textContent=`${series.length} series, ${tick(spanMs)} visible window, ${Math.max(...series.map(s=>s.p.length))} samples max`}"
    "function drawGraphs(){drawOne('g1',['t02_inlet','t03_outlet','t04_coil','t06_exhaust','t_aux_cond2']);drawOne('g2',['r01_setpoint_cooling','r02_setpoint_heating','r03_setpoint_auto','r08_min_cool_setpoint','r09_max_cooling_setpoint','r10_min_heating_setpoint','r11_max_heating_setpoint']);drawOne('g3',['r04_return_diff_cooling','r05_shutdown_temp_diff_when_cooling','r06_return_diff_heating','r07_shutdown_diff_heating','d03_defrosting_cycle_time_minutes','d04_max_defrost_time_minutes','d05_min_economy_defrost_time_minutes']);drawOne('g4',['f02_fan_high_speed_cool_setpoint','f03_fan_low_speed_temp_in_cooling_set_point','f04_fan_stop_temp_in_cooling_set_point','f05_fan_high_speed_temp_in_heating_set_point','f06_fan_low_speed_temp_in_heating_set_point','f07_fan_stop_temp_in_heating_set_point','f08_fan_low_speed_running_time','f09_fan_stop_low_speed_running_time','f12_min_fan_voltage_pct','f13_max_fan_voltage_pct'])}"
    "let base=location.pathname.replace(/\\/$/,'');"
    "async function load(){state=await fetch(base+'/state.json').then(r=>r.json());render()}"
    "async function refresh(){if(refreshPending)return;refreshPending=true;try{let prev=state.graphs||{};let next=await fetch(base+'/state.json').then(r=>r.json());if(!Object.keys(next.graphs||{}).length)next.graphs=prev;state=next;state.local_sync_ms=Date.now();state.fields.forEach(f=>{if(f.changed)changed[f.id]=Date.now()+4000});render()}finally{refreshPending=false}}async function refreshGraphs(){try{let g=await fetch(base+'/graphs.json').then(r=>r.json());state.graphs=g.graphs||{};if(g.meta&&g.meta.uptime_ms){state.meta.uptime_ms=g.meta.uptime_ms;state.local_sync_ms=Date.now()}render()}catch(e){}}saveAnns(anns());refresh().then(refreshGraphs);setInterval(refresh,2500);setInterval(refreshGraphs,10000);setInterval(render,1000);addEventListener('resize',drawGraphs);"
    "</script></body></html>";

void append_json_pair(std::ostringstream& out, const char* key, const std::string& value, bool comma = true) {
    out << "\"" << key << "\":\"" << HWPWebDashboard::escape_json(value) << "\"";
    if (comma) out << ",";
}

void append_json_string(std::string& out, const std::string& value) {
    out += "\"";
    for (char ch : value) {
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(ch) >= 0x20) out += ch;
        }
    }
    out += "\"";
}

void append_json_pair(std::string& out, const char* key, const std::string& value, bool comma = true) {
    out += "\"";
    out += key;
    out += "\":";
    append_json_string(out, value);
    if (comma) out += ",";
}

void append_uint(std::string& out, uint32_t value) {
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%" PRIu32, value);
    out += buffer;
}

void append_float(std::string& out, float value) {
    char buffer[24];
    snprintf(buffer, sizeof(buffer), "%.6g", value);
    out += buffer;
}

void append_bool(std::string& out, bool value) {
    out += value ? "true" : "false";
}

void append_bytes_json(std::string& out, const std::vector<uint8_t>& bytes) {
    out += "[";
    for (size_t i = 0; i < bytes.size(); i++) {
        if (i) out += ",";
        char buffer[8];
        snprintf(buffer, sizeof(buffer), "%u", static_cast<unsigned>(bytes[i]));
        out += buffer;
    }
    out += "]";
}

} // namespace

#ifndef HWP_NATIVE_TEST
#ifdef USE_WEBSERVER
class HWPWebHandler : public AsyncWebHandler {
  public:
    HWPWebHandler(HWPWebDashboard* dashboard, std::string path)
        : dashboard_(dashboard), path_(std::move(path)) {}
    bool canHandle(AsyncWebServerRequest* request) const override {
        if (request->method() != HTTP_GET) return false;
        char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
        auto url = request->url_to(url_buf);
        return url == path_ || url == path_ + "/" || url == path_ + "/state.json" ||
               url == path_ + "/graphs.json";
    }
    void handleRequest(AsyncWebServerRequest* request) override {
        char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
        auto url = request->url_to(url_buf);
        if (url == path_ + "/state.json") {
            auto payload = dashboard_->state_json();
            request->send(200, "application/json", payload.c_str());
            return;
        }
        if (url == path_ + "/graphs.json") {
            auto payload = dashboard_->graph_state_json();
            request->send(200, "application/json", payload.c_str());
            return;
        }
        request->send(200, "text/html", HWPWebDashboard::index_html());
    }

  private:
    HWPWebDashboard* dashboard_;
    std::string path_;
};

void HWPWebDashboard::setup(web_server::WebServer* web_server) {
    if (!this->config_.enabled || this->handlers_registered_) return;
    auto* base = web_server_base::global_web_server_base;
    if (base == nullptr || web_server == nullptr) return;
    this->web_server_ = web_server;
    base->add_handler(new HWPWebHandler(this, this->config_.path));
    this->handlers_registered_ = true;
}

void HWPWebDashboard::loop() {
    if (this->events_ != nullptr) {
        this->events_->loop();
    }
    bool should_send = false;
    {
        std::lock_guard<std::mutex> lock(this->data_mutex_);
        should_send = this->dirty_ && millis() - this->last_event_ms_ >= 250;
    }
    if (this->events_ != nullptr && should_send) {
        auto payload = this->event_json();
        // ESP-IDF build fix: web_server_idf's AsyncEventSource declares
        //   try_send_nodefer(const char *message, size_t message_len,
        //                    const char *event = nullptr, ...)
        // The two-argument Arduino-style call passed "state" where the length
        // was expected, which fails to compile under esp-idf (invalid
        // conversion from const char* to size_t).
        this->events_->try_send_nodefer(payload.c_str(), payload.size(), "state");
        {
            std::lock_guard<std::mutex> lock(this->data_mutex_);
            this->dirty_ = false;
            this->last_event_ms_ = millis();
        }
    }
}
#endif
#ifndef USE_WEBSERVER
void HWPWebDashboard::loop() {}
#endif
#endif

void HWPWebDashboard::configure(const HWPWebConfig& config) {
    this->config_ = config;
    this->config_.packet_buffer_size =
        std::min(this->config_.packet_buffer_size, HWP_WEB_MAX_PACKET_BUFFER_SIZE);
    this->config_.graph_history_size =
        std::min(this->config_.graph_history_size, HWP_WEB_MAX_GRAPH_HISTORY_SIZE);
}

void HWPWebDashboard::record_packet(const BaseFrame& frame, const std::string& kind) {
    if (!this->config_.enabled) return;
    PacketRecord record;
    record.sequence = ++this->packet_sequence_;
    record.seen_ms = millis();
    record.kind = kind;
    record.label = frame.type_string();
    record.source = frame_source_to_string(frame.get_source());
    record.frame = "0x" + std::string(BaseFrame::format_hex(frame.packet.data[0]));
    record.length = frame.packet.data_len;
    record.checksum_valid = frame.packet.is_checksum_valid();
    record.changed = frame.is_changed();
    record.bytes.reserve(frame.packet.data_len);
    for (size_t i = 0; i < frame.packet.data_len; i++) {
        record.bytes.push_back(frame.packet.data[i]);
    }
    auto key = bytes_to_key(frame);
    std::lock_guard<std::mutex> lock(this->data_mutex_);
    auto previous = this->previous_packet_bytes_.find(key);
    for (size_t i = 0; i < record.bytes.size(); i++) {
        record.changed_bytes.push_back(previous != this->previous_packet_bytes_.end() &&
                                       (i >= previous->second.size() || previous->second[i] != record.bytes[i]));
    }
    this->previous_packet_bytes_[key] = record.bytes;
    if (!this->packets_.empty()) {
        auto& last = this->packets_.back();
        if (last.source == record.source && last.frame == record.frame &&
            last.length == record.length && last.bytes == record.bytes &&
            record.seen_ms >= last.seen_ms &&
            record.seen_ms - last.seen_ms <= HWP_WEB_REPEAT_COLLAPSE_MS) {
            last.sequence = record.sequence;
            last.seen_ms = record.seen_ms;
            last.kind = record.kind;
            last.changed = false;
            std::fill(last.changed_bytes.begin(), last.changed_bytes.end(), false);
            this->latest_frames_[key] = last;
            this->dirty_ = true;
            return;
        }
    }
    this->packets_.push_back(record);
    this->latest_frames_[key] = record;
    this->trim_packets();
    this->dirty_ = true;
}

const char* HWPWebDashboard::index_html() {
    return HWP_WEB_INDEX;
}

void HWPWebDashboard::update_fields(
    const heat_pump_data_t& data, const std::string& status, bus_mode_t bus_mode) {
    // CC207S-V2.0: stubbed out. This function is never called -- the
    // dashboard is never instantiated/wired by CC207SClimate (the working
    // web UI comes from ESPHome's own web_server: component instead, see
    // cc207s_climate.h). It touched ~30 fields of the original Hayward
    // PC1000/PC1001 heat_pump_data_t (t02/t03/r01..r11/f02..f13/etc.) that
    // no longer exist in the CC207S-only Schema.h. Kept as a no-op stub,
    // rather than deleted, purely so this file continues to satisfy the
    // signature Bus.cpp's guarded (never-taken) call site expects.
    (void) data;
    (void) status;
    (void) bus_mode;
}


void HWPWebDashboard::append_field(std::vector<HWPWebField>& fields, HWPWebField field) {
    if (field.id.empty()) return;
    auto previous = this->previous_field_values_.find(field.id);
    const bool first_seen = previous == this->previous_field_values_.end();
    field.changed = !first_seen && previous->second != field.value;
    if (first_seen || field.changed) {
        field.seen_ms = millis();
        this->previous_field_values_[field.id] = field.value;
        this->previous_field_seen_ms_[field.id] = field.seen_ms;
        this->append_graph_point(field);
    } else {
        auto previous_seen = this->previous_field_seen_ms_.find(field.id);
        field.seen_ms = previous_seen == this->previous_field_seen_ms_.end() ? millis()
                                                                             : previous_seen->second;
    }
    fields.push_back(field);
}

std::string HWPWebDashboard::state_json() const {
    return this->state_json_(false);
}

std::string HWPWebDashboard::graph_state_json() const {
    std::map<std::string, std::vector<GraphPoint>> graphs;
    std::string status;
    bus_mode_t bus_mode;
    {
        std::lock_guard<std::mutex> lock(this->data_mutex_);
        graphs = this->graph_history_;
        status = this->last_status_;
        bus_mode = this->last_bus_mode_;
    }
    std::ostringstream out;
    out << "{";
    out << "\"meta\":" << meta_json(status, bus_mode) << ",";
    out << "\"graphs\":";
    append_graph_json(out, graphs);
    out << "}";
    return out.str();
}

std::string HWPWebDashboard::event_json() const {
    std::lock_guard<std::mutex> lock(this->data_mutex_);
    char buffer[160];
    snprintf(buffer, sizeof(buffer),
        "{\"revision\":\"%s\",\"packet_sequence\":%" PRIu32 ",\"field_sequence\":%" PRIu32
        ",\"uptime_ms\":%" PRIu32 "}",
        HWP_COMPONENT_VERSION, this->packet_sequence_, this->field_sequence_, millis());
    return std::string(buffer);
}

std::string HWPWebDashboard::state_json_(bool include_graphs) const {
    std::vector<HWPWebField> fields;
    std::vector<PacketRecord> frames;
    std::vector<PacketRecord> packets;
    std::map<std::string, std::vector<GraphPoint>> graphs;
    std::string status;
    bus_mode_t bus_mode;
    {
        std::lock_guard<std::mutex> lock(this->data_mutex_);
        fields = this->fields_;
        packets = this->packets_;
        frames.reserve(this->latest_frames_.size());
        for (const auto& entry : this->latest_frames_) {
            frames.push_back(entry.second);
        }
        if (include_graphs) {
            graphs = this->graph_history_;
        }
        status = this->last_status_;
        bus_mode = this->last_bus_mode_;
    }

    if (packets.size() > HWP_WEB_STATE_PACKET_LIMIT) {
        packets.erase(packets.begin(), packets.end() - HWP_WEB_STATE_PACKET_LIMIT);
    }

    std::string out;
    out.reserve(16384);
    out += "{";
    out += "\"meta\":";
    out += meta_json(status, bus_mode);
    out += ",\"fields\":";
    append_fields_json(out, fields);
    out += ",\"frames\":";
    append_packets_json(out, frames);
    out += ",\"packets\":";
    append_packets_json(out, packets);
    out += ",\"graphs\":{}";
    out += "}";
    return out;
}

size_t HWPWebDashboard::packet_count() const {
    std::lock_guard<std::mutex> lock(this->data_mutex_);
    return this->packets_.size();
}

size_t HWPWebDashboard::latest_frame_count() const {
    std::lock_guard<std::mutex> lock(this->data_mutex_);
    return this->latest_frames_.size();
}

std::string HWPWebDashboard::fields_json() const {
    std::ostringstream out;
    append_fields_json(out, this->fields_);
    return out.str();
}

void HWPWebDashboard::append_fields_json(
    std::ostringstream& out, const std::vector<HWPWebField>& fields) {
    out << "[";
    for (size_t i = 0; i < fields.size(); i++) {
        const auto& field = fields[i];
        if (i) out << ",";
        out << "{";
        append_json_pair(out, "id", field.id);
        append_json_pair(out, "label", field.label);
        append_json_pair(out, "value", field.value);
        append_json_pair(out, "unit", field.unit);
        append_json_pair(out, "group", field.group);
        append_json_pair(out, "frame", field.frame);
        append_json_pair(out, "raw", field.raw_location);
        append_json_pair(out, "source", field.source);
        out << "\"numeric\":" << bool_json(field.numeric) << ",";
        out << "\"numeric_value\":" << field.numeric_value << ",";
        out << "\"changed\":" << bool_json(field.changed) << ",";
        out << "\"seen_ms\":" << field.seen_ms;
        out << "}";
    }
    out << "]";
}

void HWPWebDashboard::append_fields_json(
    std::string& out, const std::vector<HWPWebField>& fields) {
    out += "[";
    for (size_t i = 0; i < fields.size(); i++) {
        const auto& field = fields[i];
        if (i) out += ",";
        out += "{";
        append_json_pair(out, "id", field.id);
        append_json_pair(out, "label", field.label);
        append_json_pair(out, "value", field.value);
        append_json_pair(out, "unit", field.unit);
        append_json_pair(out, "group", field.group);
        append_json_pair(out, "frame", field.frame);
        append_json_pair(out, "raw", field.raw_location);
        append_json_pair(out, "source", field.source);
        out += "\"numeric\":";
        append_bool(out, field.numeric);
        out += ",\"numeric_value\":";
        append_float(out, field.numeric_value);
        out += ",\"changed\":";
        append_bool(out, field.changed);
        out += ",\"seen_ms\":";
        append_uint(out, field.seen_ms);
        out += "}";
    }
    out += "]";
}

std::string HWPWebDashboard::packets_json() const {
    std::ostringstream out;
    append_packets_json(out, this->packets_);
    return out.str();
}

void HWPWebDashboard::append_packets_json(
    std::ostringstream& out, const std::vector<PacketRecord>& packets) {
    out << "[";
    for (size_t i = 0; i < packets.size(); i++) {
        if (i) out << ",";
        append_packet_json(out, packets[i]);
    }
    out << "]";
}

void HWPWebDashboard::append_packets_json(
    std::string& out, const std::vector<PacketRecord>& packets) {
    out += "[";
    for (size_t i = 0; i < packets.size(); i++) {
        if (i) out += ",";
        append_packet_json(out, packets[i]);
    }
    out += "]";
}

std::string HWPWebDashboard::frames_json() const {
    std::ostringstream out;
    out << "[";
    size_t index = 0;
    for (const auto& entry : this->latest_frames_) {
        if (index++) out << ",";
        append_packet_json(out, entry.second);
    }
    out << "]";
    return out.str();
}

void HWPWebDashboard::append_packet_json(std::ostringstream& out, const PacketRecord& packet) {
    out << "{";
    out << "\"sequence\":" << packet.sequence << ",";
    out << "\"seen_ms\":" << packet.seen_ms << ",";
    append_json_pair(out, "kind", packet.kind);
    append_json_pair(out, "label", packet.label);
    append_json_pair(out, "source", packet.source);
    append_json_pair(out, "frame", packet.frame);
    out << "\"length\":" << packet.length << ",";
    out << "\"checksum_valid\":" << bool_json(packet.checksum_valid) << ",";
    out << "\"changed\":" << bool_json(packet.changed) << ",";
    out << "\"bytes\":" << bytes_to_json(packet.bytes) << ",";
    out << "\"changed_bytes\":[";
    for (size_t j = 0; j < packet.changed_bytes.size(); j++) {
        if (j) out << ",";
        out << bool_json(packet.changed_bytes[j]);
    }
    out << "]";
    out << "}";
}

void HWPWebDashboard::append_packet_json(std::string& out, const PacketRecord& packet) {
    out += "{";
    out += "\"sequence\":";
    append_uint(out, packet.sequence);
    out += ",\"seen_ms\":";
    append_uint(out, packet.seen_ms);
    out += ",";
    append_json_pair(out, "kind", packet.kind);
    append_json_pair(out, "label", packet.label);
    append_json_pair(out, "source", packet.source);
    append_json_pair(out, "frame", packet.frame);
    out += "\"length\":";
    append_uint(out, packet.length);
    out += ",\"checksum_valid\":";
    append_bool(out, packet.checksum_valid);
    out += ",\"changed\":";
    append_bool(out, packet.changed);
    out += ",\"bytes\":";
    append_bytes_json(out, packet.bytes);
    out += ",\"changed_bytes\":[";
    for (size_t i = 0; i < packet.changed_bytes.size(); i++) {
        if (i) out += ",";
        append_bool(out, packet.changed_bytes[i]);
    }
    out += "]}";
}

std::string HWPWebDashboard::graph_json() const {
    std::ostringstream out;
    append_graph_json(out, this->graph_history_);
    return out.str();
}

void HWPWebDashboard::append_graph_json(
    std::ostringstream& out, const std::map<std::string, std::vector<GraphPoint>>& graphs) {
    out << "{";
    size_t field_index = 0;
    for (const auto& entry : graphs) {
        if (field_index++) out << ",";
        out << "\"" << escape_json(entry.first) << "\":[";
        for (size_t i = 0; i < entry.second.size(); i++) {
            if (i) out << ",";
            out << "{\"sequence\":" << entry.second[i].sequence << ",\"t\":"
                << entry.second[i].seen_ms << ",\"value\":" << entry.second[i].value << "}";
        }
        out << "]";
    }
    out << "}";
}

std::string HWPWebDashboard::meta_json(const std::string& status, bus_mode_t bus_mode) const {
    std::ostringstream out;
    out << "{";
    append_json_pair(out, "revision", HWP_COMPONENT_VERSION);
    append_json_pair(out, "status", status);
    append_json_pair(out, "bus_mode", bus_mode_to_string(bus_mode));
    out << "\"uptime_ms\":" << millis();
    out << "}";
    return out.str();
}

void HWPWebDashboard::append_graph_point(const HWPWebField& field) {
    if (!field.numeric) return;
    auto& points = this->graph_history_[field.id];
    points.push_back(GraphPoint{++this->field_sequence_, field.seen_ms, field.numeric_value});
    trim_graph(field.id);
}

void HWPWebDashboard::trim_packets() {
    if (this->packets_.size() > this->config_.packet_buffer_size) {
        this->packets_.erase(this->packets_.begin(),
            this->packets_.begin() + (this->packets_.size() - this->config_.packet_buffer_size));
    }
}

void HWPWebDashboard::trim_graph(const std::string& field_id) {
    auto& points = this->graph_history_[field_id];
    if (points.size() > this->config_.graph_history_size) {
        points.erase(points.begin(), points.begin() + (points.size() - this->config_.graph_history_size));
    }
}

size_t HWPWebDashboard::graph_point_count(const std::string& field_id) const {
    std::lock_guard<std::mutex> lock(this->data_mutex_);
    auto found = this->graph_history_.find(field_id);
    return found == this->graph_history_.end() ? 0 : found->second.size();
}

HWPWebField HWPWebDashboard::make_float_field(const char* id, const char* label,
    optional<float> value, const char* unit, const char* group, const char* frame,
    const char* raw_location, const char* source) {
    if (!value.has_value()) return {};
    char formatted[24];
    snprintf(formatted, sizeof(formatted), "%.1f", value.value());
    return HWPWebField{id, label, formatted, unit, group, frame, raw_location, source, true,
        value.value(), false, 0};
}

HWPWebField HWPWebDashboard::make_string_field(const char* id, const char* label,
    const std::string& value, const char* group, const char* frame, const char* raw_location,
    const char* source) {
    return HWPWebField{id, label, value, "", group, frame, raw_location, source, false, 0.0f,
        false, 0};
}

std::string HWPWebDashboard::escape_json(const std::string& value) {
    std::string escaped;
    for (char ch : value) {
        switch (ch) {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (static_cast<unsigned char>(ch) >= 0x20) escaped += ch;
        }
    }
    return escaped;
}

std::string HWPWebDashboard::bus_mode_to_string(bus_mode_t mode) {
    switch (mode) {
    case BUSMODE_TX: return "TX";
    case BUSMODE_RX: return "RX";
    case BUSMODE_ERROR: return "ERROR";
    }
    return "UNKNOWN";
}

std::string HWPWebDashboard::frame_source_to_string(frame_source_t source) {
    switch (source) {
    case SOURCE_HEATER: return "heater";
    case SOURCE_CONTROLLER: return "controller";
    case SOURCE_LOCAL: return "local";
    case SOURCE_UNKNOWN: return "unknown";
    }
    return "unknown";
}

std::string HWPWebDashboard::bytes_to_key(const BaseFrame& frame) {
    std::ostringstream out;
    out << static_cast<int>(frame.packet.data[0]) << ":" << frame_source_to_string(frame.get_source())
        << ":" << frame.packet.data_len;
    return out.str();
}

std::string HWPWebDashboard::bytes_to_json(const std::vector<uint8_t>& bytes) {
    std::ostringstream out;
    out << "[";
    for (size_t i = 0; i < bytes.size(); i++) {
        if (i) out << ",";
        out << static_cast<int>(bytes[i]);
    }
    out << "]";
    return out.str();
}

} // namespace hwp
} // namespace esphome
