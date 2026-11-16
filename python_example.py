import threading
import time

from pipeipc import Client, Server, SharedVars


def wait_until(predicate, description, timeout=5.0):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if predicate():
            return
        time.sleep(0.01)
    raise TimeoutError(f"Timed out waiting for {description}")


def main():
    server = Server("pipeipc-example")
    first = Client("pipeipc-example")
    second = Client("pipeipc-example")
    server.start()
    try:
        first.connect()
        second.connect()
        first_vars = SharedVars(first)
        second_vars = SharedVars(second)
        try:
            first_vars.status = {"owner": "first", "ready": True}
            wait_until(lambda: second_vars.get("status") == {"owner": "first", "ready": True},
                       "first client's shared-variable update")

            second_vars.status = "changed by the second client"
            wait_until(lambda: first_vars.get("status") == "changed by the second client",
                       "second client's write to the same variable")

            received = threading.Event()
            messages = []

            def on_message(label, value):
                messages.append((label, value))
                received.set()

            second.on_value("example.message", on_message)
            first.send("example.message", {"text": "hello", "count": 3})
            if not received.wait(5):
                raise TimeoutError("Timed out waiting for the message")
            print("Message:", messages[0])
            print("Shared variables:", first_vars.snapshot())
        finally:
            first_vars.delete("status")
            first_vars.close()
            second_vars.close()
            first.disconnect()
            second.disconnect()
    finally:
        server.stop()


if __name__ == "__main__":
    main()
