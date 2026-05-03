use std::sync::{Arc, Mutex, mpsc::Sender};

use rouille::{Request, Response, ResponseBody, Server};

use crate::login::{Main, UnlockCredentials};

pub fn run(main: Arc<Mutex<Main>>) -> Sender<()> {
    let addr = ("10.8.1.9", 25563);

    let handler = move |request: &Request| {
        let username = request.get_param("username").unwrap();
        let password = request.get_param("password").unwrap();

        let args = UnlockCredentials { username, password };

        main.lock().unwrap().unlock(args);

        Response {
            status_code: 200,
            headers: vec![],
            data: ResponseBody::empty(),
            upgrade: None,
        }
    };

    let server = Server::new(addr, handler).unwrap();

    let (_, sender) = server.stoppable();

    sender
}
