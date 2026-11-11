#!/usr/bin/env node
// SPDX-License-Identifier: MIT
// Follow the Surface Dial from JavaScript through OpenDial's local HTTP API
// (Node 18+, no dependencies).
//
// 1. Enable it: OpenDial → Integrations → Local HTTP API (or set
//    settings.integrations.httpApi.enabled = true).
// 2. node dial-events.mjs            # print gestures and tool changes
//    node dial-events.mjs tool zoom  # switch the active tool
//
// The token lives in $XDG_RUNTIME_DIR/opendial/http-token (mode 0600); the API
// only listens on 127.0.0.1 and refuses browser (Origin) requests.
import { readFileSync } from "node:fs";

const port = Number(process.env.OPENDIAL_HTTP_PORT ?? 47311);
const runtime = process.env.XDG_RUNTIME_DIR ?? `/run/user/${process.getuid()}`;
const token = readFileSync(`${runtime}/opendial/http-token`, "utf8").trim();
const base = `http://127.0.0.1:${port}/v1`;
const headers = { Authorization: `Bearer ${token}` };

async function api(path, body) {
  const res = await fetch(base + path, body === undefined ? { headers } : {
    method: "POST", headers: { ...headers, "Content-Type": "application/json" }, body: JSON.stringify(body),
  });
  const json = await res.json();
  if (!res.ok) throw new Error(`${path}: ${json.error ?? res.status}`);
  return json;
}

async function follow() {
  const state = await api("/state");
  console.log(`tool: ${state.activeToolName} (${state.profileName})`);
  const res = await fetch(`${base}/events`, { headers });
  if (!res.ok) throw new Error(`events: ${res.status}`);
  const decoder = new TextDecoder();
  let buffer = "";
  for await (const chunk of res.body) {
    buffer += decoder.decode(chunk, { stream: true });
    let end;
    while ((end = buffer.indexOf("\n\n")) >= 0) {
      const block = buffer.slice(0, end);
      buffer = buffer.slice(end + 2);
      const event = /^event: (.*)$/m.exec(block)?.[1];
      const data = /^data: (.*)$/m.exec(block)?.[1];
      if (!event || !data) continue; // keep-alive comment
      const e = JSON.parse(data);
      if (event === "tool") console.log(`tool: ${e.toolName ?? e.tool} [${e.profile}]`);
      else if (e.kind === "rotate") console.log(`turn ${e.degrees > 0 ? "+" : ""}${e.degrees.toFixed(1)}°${e.pressed ? " (pressed)" : ""}`);
      else console.log(e.kind + (e.count ? ` x${e.count}` : ""));
    }
  }
}

const [cmd, arg] = process.argv.slice(2);
if (cmd === "tool") console.log(await api("/tool", { id: arg }));
else if (cmd === "menu") console.log(await api("/menu", { op: arg ?? "toggle" }));
else await follow();
