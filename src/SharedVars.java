package pipeipc;

import java.util.Map;

/**
 * Process-shared variables backed by the connected PipeIPC server.
 * Values support null, Boolean, integral/floating numbers, String, byte[],
 * List, Set, and Map<String, ?>. Any connected client may change any key.
 */
public final class SharedVars implements AutoCloseable {
    static { System.loadLibrary("pipeipc_jni"); }
    private final Client client;
    private long handle;

    public SharedVars(Client client) {
        if (client == null) throw new NullPointerException("client");
        this.client = client;
        handle = nativeCreate(client.nativeHandle());
        if (handle == 0) throw new IllegalStateException("Could not create shared variables");
    }
    public synchronized void set(String key, Object value) { ensureOpen(); nativeWrite(handle, key, value); }
    public synchronized Object get(String key) { ensureOpen(); return nativeRead(handle, key); }
    public synchronized void delete(String key) { ensureOpen(); nativeDelete(handle, key); }
    public synchronized boolean contains(String key) { ensureOpen(); return nativeContains(handle, key); }
    @SuppressWarnings("unchecked")
    public synchronized Map<String,Object> snapshot() { ensureOpen(); return (Map<String,Object>) nativeSnapshot(handle); }

    private void ensureOpen() { if (handle == 0) throw new IllegalStateException("SharedVars is closed"); }
    @Override public synchronized void close() {
        if (handle != 0) { nativeClose(handle); handle = 0; }
    }
    private static native long nativeCreate(long clientHandle);
    private static native void nativeWrite(long handle, String key, Object value);
    private static native Object nativeRead(long handle, String key);
    private static native void nativeDelete(long handle, String key);
    private static native boolean nativeContains(long handle, String key);
    private static native Object nativeSnapshot(long handle);
    private static native void nativeClose(long handle);
}
