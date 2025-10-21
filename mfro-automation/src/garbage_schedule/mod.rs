use std::{
    collections::HashMap,
    sync::{Arc, Mutex},
};

use anyhow::Result;
use chrono::{DateTime, Duration, NaiveDate, Utc};
use icalendar::{Alarm, Calendar, Component, Event, EventLike};
use reqwest::{Client, Error};
use rouille::Response;
use serde::{Deserialize, Serialize};

use crate::util::default;

const BASE_URL: &str = "https://myutilities.seattle.gov/rest";

#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
pub struct Config {
    address: String,
    port: u16,
}

#[derive(Clone, Debug)]
pub struct UtilitiesClient {
    client: Client,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
struct AddressRequest<'a> {
    #[serde(borrow)]
    address: Address<'a>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
struct Address<'a> {
    #[serde(rename = "addressLine1")]
    address_line1: &'a str,
    city: &'a str,
    zip: &'a str,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
struct AddressResponse {
    address: Vec<AddressEntry>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
struct AddressEntry {
    #[serde(rename = "premCode")]
    prem_code: String,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
struct AccountRequest<'a> {
    #[serde(borrow)]
    address: PremCode<'a>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
struct PremCode<'a> {
    #[serde(rename = "premCode")]
    prem_code: &'a str,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
struct AccountResponse {
    account: Account,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
struct Account {
    #[serde(rename = "accountNumber")]
    account_number: String,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
struct AuthRequest<'a> {
    grant_type: &'a str,
    username: &'a str,
    password: &'a str,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
struct AuthResponse {
    #[serde(rename = "access_token")]
    access_token: String,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
struct SummaryRequest<'a> {
    #[serde(rename = "customerId")]
    customer_id: &'a str,
    #[serde(rename = "accountContext")]
    account_context: AccountContext<'a>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
struct AccountContext<'a> {
    #[serde(rename = "accountNumber")]
    account_number: &'a str,
    #[serde(rename = "personId")]
    person_id: Option<&'a str>,
    #[serde(rename = "companyCd")]
    company_cd: Option<&'a str>,
    #[serde(rename = "serviceAddress")]
    service_address: Option<&'a str>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
struct SummaryResponse {
    #[serde(rename = "accountContext")]
    account_context: ContextDetails,
    #[serde(rename = "accountSummaryType")]
    account_summary_type: AccountSummaryType,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
struct ContextDetails {
    #[serde(rename = "personId")]
    person_id: String,
    #[serde(rename = "companyCd")]
    company_cd: String,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
struct AccountSummaryType {
    #[serde(rename = "swServices")]
    sw_services: Vec<SwService>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
struct SwService {
    services: Vec<Service>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
struct Service {
    #[serde(rename = "description")]
    description: String,
    #[serde(rename = "servicePointId")]
    service_point_id: String,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
struct CalendarRequest<'a> {
    #[serde(rename = "customerId")]
    customer_id: &'a str,
    #[serde(rename = "accountContext")]
    account_context: AccountContext<'a>,
    #[serde(rename = "servicePoints")]
    service_points: Vec<&'a str>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
struct CalendarResponse {
    calendar: HashMap<String, Vec<String>>,
}

impl UtilitiesClient {
    pub fn new() -> Self {
        Self {
            client: Client::new(),
        }
    }

    async fn find_address(&self, address: &str) -> Result<String, Error> {
        let payload = AddressRequest {
            address: Address {
                address_line1: address,
                city: "",
                zip: "",
            },
        };

        let resp = self
            .client
            .post(format!("{BASE_URL}/serviceorder/findaddress"))
            .json(&payload)
            .send()
            .await?
            .json::<AddressResponse>()
            .await?;

        Ok(resp.address[0].prem_code.clone())
    }

    async fn find_account(&self, prem_code: &str) -> Result<String, Error> {
        let payload = AccountRequest {
            address: PremCode { prem_code },
        };

        let resp = self
            .client
            .post(format!("{BASE_URL}/serviceorder/findAccount"))
            .json(&payload)
            .send()
            .await?
            .json::<AccountResponse>()
            .await?;

        Ok(resp.account.account_number)
    }

    async fn authenticate_guest(&self) -> Result<String, Error> {
        let payload = AuthRequest {
            grant_type: "password",
            username: "guest",
            password: "guest",
        };

        let resp = self
            .client
            .post(format!("{BASE_URL}/auth/guest"))
            .json(&payload)
            .send()
            .await?
            .json::<AuthResponse>()
            .await?;

        Ok(resp.access_token)
    }

    async fn get_solid_waste_summary(
        &self,
        auth: &str,
        account_number: &str,
    ) -> Result<(String, String, Vec<Service>), Error> {
        let payload = SummaryRequest {
            customer_id: "guest",
            account_context: AccountContext {
                account_number,
                person_id: None,
                company_cd: None,
                service_address: None,
            },
        };

        let resp = self
            .authed_post("/guest/swsummary", auth, &payload)
            .await?
            .json::<SummaryResponse>()
            .await?;

        let services = resp.account_summary_type.sw_services[0].services.clone();
        Ok((
            resp.account_context.person_id,
            resp.account_context.company_cd,
            services,
        ))
    }

    async fn get_solid_waste_calendar(
        &self,
        auth: &str,
        account_number: &str,
        person_id: &str,
        company_cd: &str,
        services: &[Service],
    ) -> Result<CalendarResponse, Error> {
        let service_points: Vec<&str> = services
            .iter()
            .map(|s| s.service_point_id.as_str())
            .collect();

        let payload = CalendarRequest {
            customer_id: "guest",
            account_context: AccountContext {
                account_number,
                person_id: Some(person_id),
                company_cd: Some(company_cd),
                service_address: None,
            },
            service_points,
        };

        let resp = self
            .authed_post("/solidwastecalendar", auth, &payload)
            .await?
            .json::<CalendarResponse>()
            .await?;

        Ok(resp)
    }

    async fn authed_post<T: Serialize + ?Sized>(
        &self,
        path: &str,
        auth: &str,
        body: &T,
    ) -> Result<reqwest::Response, Error> {
        self.client
            .post(format!("{BASE_URL}{path}"))
            .bearer_auth(auth)
            .json(body)
            .send()
            .await
    }

    fn generate_ics(&self, services: &[Service], calendar: &CalendarResponse) -> String {
        let mut dates: HashMap<String, Vec<String>> = HashMap::new();

        for service in services {
            let id = &service.service_point_id;
            let name = &service.description;
            if let Some(entries) = calendar.calendar.get(id) {
                for date in entries {
                    dates.entry(date.clone()).or_default().push(name.clone());
                }
            }
        }

        let mut cal = Calendar::new();

        for (date_str, names) in dates {
            let parts: Vec<u32> = date_str
                .split('/')
                .filter_map(|s| s.parse::<u32>().ok())
                .collect();
            if parts.len() != 3 {
                continue;
            }
            let (month, day, year) = (parts[0], parts[1], parts[2]);
            let date = NaiveDate::from_ymd_opt(year as i32, month, day);
            if let Some(date) = date {
                let mut event = Event::new();
                event.summary(&names.join(", "));
                event.starts(date);

                let alarm = Alarm::display("Take out the bins", Duration::hours(-6));
                event.alarm(alarm);

                cal.push(event);
            }
        }

        cal.to_string()
    }

    #[tokio::main]
    pub async fn get_calendar(&self, address: &str) -> Result<String> {
        let prem_code = self.find_address(address).await?;
        let account_number = self.find_account(&prem_code).await?;

        let auth = self.authenticate_guest().await?;

        let (person_id, company_cd, services) =
            self.get_solid_waste_summary(&auth, &account_number).await?;

        let calendar_response = self
            .get_solid_waste_calendar(&auth, &account_number, &person_id, &company_cd, &services)
            .await?;

        let ics = self.generate_ics(&services, &calendar_response);

        Ok(ics)
    }
}

struct Cache<T> {
    value: Option<T>,
    expiration: DateTime<Utc>,
    provider: Box<dyn Send + Sync + Fn() -> T>,
    timeout: Duration,
}

impl<T> Cache<T> {
    fn new(timeout: Duration, provider: Box<dyn Send + Sync + Fn() -> T>) -> Self {
        let value = None;
        let expiration = default();

        Self {
            value,
            expiration,
            provider,
            timeout,
        }
    }

    pub fn get(&mut self) -> &T {
        let now = Utc::now();

        if now >= self.expiration {
            self.value = None;
        }

        self.value.get_or_insert_with(|| {
            self.expiration = now + self.timeout;
            (self.provider)()
        })
    }
}

#[tokio::main]
pub async fn run(config: Config) -> Result<()> {
    let client = UtilitiesClient::new();

    let address = config.address.to_owned();

    let cache = Arc::new(Mutex::new(Cache::new(
        Duration::hours(2),
        Box::new(move || client.get_calendar(&address).unwrap()),
    )));

    let addr = ("0.0.0.0", config.port);
    rouille::start_server(addr, move |request| {
        if request.url() == "/garbage.ics" {
            let mut cache = cache.lock().unwrap();
            let ics = cache.get();

            Response::from_data("text/calendar", ics.clone())
                .with_additional_header("content-disposition", "attachment; filename=garbage.ics")
        } else {
            Response::empty_404()
        }
    });
}
