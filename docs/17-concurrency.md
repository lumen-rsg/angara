# Concurrency

Built-in threading and synchronization types.

---

## Mutex

Use `Mutex` to protect shared state across threads:

```angara
let mutex as Mutex = Mutex();
let results as list<i64> = [];

mutex.lock();
results.push(42);
mutex.unlock();
```

## Thread

The `Thread` type supports concurrent execution:

```angara
let t as Thread = Thread();
```

## Complete Example

```angara
attach io;
attach Server, WebSocket, createServer from websocket;

let clients as list<WebSocket> = [];
let mutex as Mutex = Mutex();

func on_connect(server as Server, client as WebSocket) -> nil {
    mutex.lock();
    clients.push(client);
    mutex.unlock();
    client.send("Welcome!");
}

func on_message(server as Server, client as WebSocket, message as string) -> nil {
    mutex.lock();
    for (c in clients) {
        c.send("echo: " + message);
    }
    mutex.unlock();
}

export func main() -> i64 {
    let server = createServer(8080, {
        "on_connect": on_connect,
        "on_message": on_message
    });
    while (true) {
        server.service();
    }
}
```
