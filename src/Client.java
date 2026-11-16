package pipeipc;

/** Connects an application to a PipeIPC message broker. */
public final class Client implements AutoCloseable {
    static { System.loadLibrary("pipeipc_jni"); }
    private long handle;

    public Client() { this("default"); }
    public Client(String name) {
        handle = nativeCreate(name);
        if (handle == 0) throw new IllegalStateException("Could not create PipeIPC client");
    }
    public synchronized void connect() { ensureOpen(); nativeConnect(handle); }
    public synchronized void disconnect() { if (handle != 0) nativeDisconnect(handle); }
    public boolean isConnected() { ensureOpen(); return nativeConnected(handle); }
    public void publish(String label, byte[] payload) { ensureOpen(); nativePublish(handle, label, payload); }
    public void on(String label, MessageListener listener) {
        ensureOpen();
        if (listener == null) throw new NullPointerException("listener");
        nativeOn(handle, label, listener);
    }
    public void off(String label) { ensureOpen(); nativeOff(handle, label); }

    long nativeHandle() { ensureOpen(); return handle; }
    private void ensureOpen() { if (handle == 0) throw new IllegalStateException("Client is closed"); }
    @Override public synchronized void close() {
        if (handle != 0) { nativeDisconnect(handle); nativeClose(handle); handle = 0; }
    }
    private static native long nativeCreate(String name);
    private static native void nativeConnect(long handle);
    private static native void nativeDisconnect(long handle);
    private static native boolean nativeConnected(long handle);
    private static native void nativePublish(long handle, String label, byte[] payload);
    private static native void nativeOn(long handle, String label, MessageListener listener);
    private static native void nativeOff(long handle, String label);
    private static native void nativeClose(long handle);
}
