use anyhow::Result;
use reqwest::Client;

pub struct GatewayClient {
    client: Client,
}

impl GatewayClient {
    pub fn new() -> Self {
        let client = Client::new();

        Self { client }
    }

    #[tokio::main]
    pub async fn trigger_pc_power(&self) -> Result<()> {
        self.client
            .post("http://10.8.1.6:8123/api/webhook/pc_power")
            .send()
            .await?;

          Ok(())
    }

    #[tokio::main]
    pub async fn trigger_garage_door(&self) -> Result<()> {
        self.client
            .post("http://10.8.1.6:8123/api/webhook/garage_door")
            .send()
            .await?;

          Ok(())
    }
}
