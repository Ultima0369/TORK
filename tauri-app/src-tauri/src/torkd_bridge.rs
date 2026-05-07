//! Torkd bridge — Unix Socket client for torkd communication

use crate::common::project_root;
use std::io::{Read, Write};
use std::os::unix::net::UnixStream;
use std::time::Duration;

const RECV_BUF: usize = 65536;
const SOCKET_TIMEOUT: Duration = Duration::from_secs(2);

fn socket_path() -> std::path::PathBuf {
    project_root().join("torkd.sock")
}

/// Send a query to torkd and return the response string
pub fn query(command: &str) -> Result<String, String> {
    let path = socket_path();
    let mut stream =
        UnixStream::connect(&path).map_err(|e| format!("Connect to {:?} failed: {}", path, e))?;

    stream
        .set_read_timeout(Some(SOCKET_TIMEOUT))
        .map_err(|e| format!("Set read timeout failed: {}", e))?;
    stream
        .set_write_timeout(Some(SOCKET_TIMEOUT))
        .map_err(|e| format!("Set write timeout failed: {}", e))?;

    // Protocol: command\n
    let msg = format!("{}\n", command);
    stream
        .write_all(msg.as_bytes())
        .map_err(|e| format!("Write failed: {}", e))?;

    // Read response until connection closes or buffer full
    let mut buf = vec![0u8; RECV_BUF];
    let mut total = 0;
    loop {
        if total >= RECV_BUF {
            break;
        }
        match stream.read(&mut buf[total..]) {
            Ok(0) => break, // Connection closed
            Ok(n) => total += n,
            Err(e) => {
                // Timeout is acceptable — return what we have
                if e.kind() == std::io::ErrorKind::TimedOut
                    || e.kind() == std::io::ErrorKind::WouldBlock
                {
                    break;
                }
                return Err(format!("Read failed: {}", e));
            }
        }
    }

    String::from_utf8(buf[..total].to_vec()).map_err(|e| format!("UTF-8 decode failed: {}", e))
}
