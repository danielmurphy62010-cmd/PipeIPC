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

            # Mutate nested values in place; no reassignment is required.
            first_vars.status["ready"] = False
            first_vars.status["history"] = []
            first_vars.status["history"].append("started")
            first_vars.status["labels"] = {"alpha"}
            first_vars.status["labels"].add("beta")
            wait_until(
                lambda: second_vars.get("status", {}).get("history") == ["started"]
                and second_vars.get("status", {}).get("ready") is False
                and second_vars.get("status", {}).get("labels") == {"alpha", "beta"},
                "nested dict, list, and set mutations",
            )

            # Deleting the actual instance attribute removes the shared key.
            del first_vars.status
            wait_until(lambda: not second_vars.contains("status"),
                       "shared-variable deletion")

            second_vars.status = {"owner": "second", "items": [1]}
            second_vars.status["items"].extend([2, 3])
            wait_until(
                lambda: first_vars.get("status", {}).get("items") == [1, 2, 3],
                "second client's in-place list mutation",
            )

            received = threading.Event()
            handled = []

            second.on("example.event", lambda label, value: (handled.append((label, value)), received.set()))
            first.send("example.event", {"text": "handled by on"})
            first.send("example.recv", {"text": "received by recv"})

            message = second.recv(5000)
            if message is None:
                raise TimeoutError("Timed out waiting for recv()")
            if not received.wait(5):
                raise TimeoutError("Timed out waiting for on()")
            print("recv():", message)
            print("on():", handled[0])
            print("Shared variables:", first_vars.snapshot())
        finally:
            if first_vars.contains("status"):
                del first_vars.status
            first_vars.close()
            second_vars.close()
            first.disconnect()
            second.disconnect()
    finally:
        server.stop()


if __name__ == "__main__":
    main()
