package pipeipc;

import java.lang.reflect.Field;
import java.lang.reflect.Modifier;
import java.util.LinkedHashMap;
import java.util.Map;

public abstract class SharedVars implements AutoCloseable {
    static { System.loadLibrary("pipeipc_jni"); }
    private long handle;

    protected SharedVars(String label) { this(label, "java", true); }

    protected SharedVars(String label, String processId, boolean creator) {
        handle = nativeCreate(label, processId, creator);
        if (handle == 0) throw new IllegalStateException("Could not create SharedVars");
    }

    public final synchronized Map<String,Object> snapshot() {
        if (handle == 0) throw new IllegalStateException("SharedVars is closed");
        Map<String,Object> values = new LinkedHashMap<>();
        for (Class<?> c = getClass(); c != null && c != SharedVars.class; c = c.getSuperclass()) {
            for (Field f : c.getDeclaredFields()) {
                int m = f.getModifiers();
                if (Modifier.isStatic(m) || Modifier.isFinal(m) || Modifier.isTransient(m) || f.isSynthetic() || f.getName().startsWith("_")) continue;
                try { f.setAccessible(true); values.putIfAbsent(f.getName(), f.get(this)); }
                catch (ReflectiveOperationException | RuntimeException e) { throw new IllegalStateException("Cannot read shared field: " + f.getName(), e); }
            }
        }
        return values;
    }

    @Override public final synchronized void close() {
        if (handle != 0) { nativeClose(handle); handle = 0; }
    }

    private static native long nativeCreate(String label, String processId, boolean creator);
    private static native void nativeClose(long handle);
}
