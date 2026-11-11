// SPDX-License-Identifier: MIT
//! Minimal OpenDial plugin in Rust: registers a "Counter" tool, prints Dial
//! events for it and shows the count in OpenDial's on-screen display.
//! Approve it once: `opendialctl plugin allow org.example.rust-counter`.
use futures_util::StreamExt;
use serde_json::{json, Value};
use zbus::{proxy, Connection};

#[proxy(
    interface = "io.github.opendial.Daemon1",
    default_service = "io.github.opendial",
    default_path = "/io/github/opendial/Daemon"
)]
trait Daemon {
    fn register_app(&self, json: &str) -> zbus::Result<String>;
    fn unregister_app(&self, app_id: &str) -> zbus::Result<bool>;
    fn plugin_feedback(&self, app_id: &str, json: &str) -> zbus::Result<bool>;
    #[zbus(signal)]
    fn plugin_event(&self, app_id: String, json: String) -> zbus::Result<()>;
}

const APP_ID: &str = "org.example.rust-counter";

#[tokio::main]
async fn main() -> zbus::Result<()> {
    let conn = Connection::session().await?;
    let dial = DaemonProxy::new(&conn).await?;
    let mut events = dial.receive_plugin_event().await?;
    let reg = json!({
        "appId": APP_ID,
        "name": "Rust counter",
        "tools": [{"id": "counter", "name": "Counter (Rust)", "icon": "value"}]
    });
    println!("registered: {}", dial.register_app(&reg.to_string()).await?);

    let mut count: i64 = 0;
    loop {
        tokio::select! {
            Some(sig) = events.next() => {
                let args = sig.args()?;
                if args.app_id != APP_ID { continue; }
                let ev: Value = serde_json::from_str(&args.json).unwrap_or_default();
                match ev["slot"].as_str().unwrap_or_default() {
                    "rotate_cw" | "rotate_ccw" => count += ev["steps"].as_i64().unwrap_or(0),
                    "click" => count = 0,
                    _ => {}
                }
                println!("{} count={count}", ev["slot"]);
                let fb = json!({"osd": {"title": "Counter", "text": count.to_string()}});
                dial.plugin_feedback(APP_ID, &fb.to_string()).await?;
            }
            _ = tokio::signal::ctrl_c() => break,
        }
    }
    dial.unregister_app(APP_ID).await?;
    Ok(())
}
