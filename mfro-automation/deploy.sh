cross build --release --target x86_64-unknown-linux-musl
scp target/x86_64-unknown-linux-musl/release/mfro-automation databox:mfro-storage/server/public/mfro-automation/image/mfro-automation
