package pipeipc;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.lang.reflect.Field;
import java.lang.reflect.Modifier;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Base class for Java shared state. Declare application variables as mutable
 * instance fields in a subclass, then call startSync() from that subclass's
 * constructor after its fields have been initialized.
 *
 * A field absent from the broker hash table is created, a changed hash is
 * updated, and a broker entry without a corresponding live field is deleted.
 * Assign SharedVars.DELETE to an Object-typed field to delete it.
 */
public class SharedVars implements AutoCloseable {
    static { System.loadLibrary("pipeipc_jni"); }

    /** Assign to an Object-typed field to delete the corresponding variable. */
    public static final Object DELETE = new Object() {
        @Override public String toString() { return "SharedVars.DELETE"; }
    };

    private final Object syncLock = new Object();
    private final Client client;
    private final long pollMillis;
    private final Map<String, byte[]> hashTable = new HashMap<>();
    private final Set<String> remoteTombstones = new HashSet<>();
    private final Map<String, byte[]> tombstoneHashes = new HashMap<>();
    private final AtomicBoolean stop = new AtomicBoolean();
    private volatile long handle;
    private volatile long revision;
    private volatile boolean started;
    private volatile boolean closed;
    private volatile Throwable syncFailure;
    private Thread updater;
    private Map<String, Field> appFields;

    protected SharedVars(Client client) { this(client, 10); }

    protected SharedVars(Client client, long pollMillis) {
        if (client == null) throw new NullPointerException("client");
        if (pollMillis <= 0) throw new IllegalArgumentException("pollMillis must be positive");
        this.client = client;
        this.pollMillis = pollMillis;
        handle = nativeCreate(client.nativeHandle());
        if (handle == 0) throw new IllegalStateException("Could not create shared variables");
    }

    /** Start after subclass field initializers have run. */
    protected final void startSync() {
        synchronized (syncLock) {
            ensureOpen();
            if (started) return;
            appFields = discoverFields();
            Snapshot initial = stableSnapshot();
            for (Map.Entry<String, Object> item : initial.values.entrySet()) {
                Field field = appFields.get(item.getKey());
                if (field != null) assignRemote(field, item.getValue(), false);
            }
            hashTable.clear();
            hashTable.putAll(hashes(initial.values));
            revision = initial.revision;
            started = true;
            updater = new Thread(this::updateLoop, "PipeIPC-SharedVars");
            updater.setDaemon(true);
            updater.start();
        }
    }

    /** Diagnostic state for the updater; this is not a shared variable. */
    public final Throwable syncFailure() { return syncFailure; }

    private void ensureOpen() {
        if (closed || handle == 0) throw new IllegalStateException("SharedVars is closed");
    }

    private Map<String, Field> discoverFields() {
        Map<String, Field> fields = new TreeMap<>();
        for (Class<?> type = getClass(); type != null && type != SharedVars.class && type != Object.class;
             type = type.getSuperclass()) {
            for (Field field : type.getDeclaredFields()) {
                int modifiers = field.getModifiers();
                if (Modifier.isStatic(modifiers) || Modifier.isFinal(modifiers)
                        || Modifier.isTransient(modifiers) || field.isSynthetic()) continue;
                try {
                    field.setAccessible(true);
                } catch (RuntimeException ex) {
                    throw new IllegalStateException("Cannot access shared field " + field.getName(), ex);
                }
                if (fields.putIfAbsent(field.getName(), field) != null)
                    throw new IllegalStateException("Duplicate shared field name: " + field.getName());
            }
        }
        return fields;
    }

    private Map<String, Object> scanFields() {
        Map<String, Object> values = new TreeMap<>();
        try {
            for (Map.Entry<String, Field> item : appFields.entrySet()) {
                String name = item.getKey();
                Field field = item.getValue();
                Object value = field.get(this);
                if (value == DELETE) continue;
                if (remoteTombstones.contains(name)) {
                    if (Arrays.equals(hash(value), tombstoneHash(field))) continue;
                    remoteTombstones.remove(name);
                    tombstoneHashes.remove(name);
                }
                values.put(name, value);
            }
            return values;
        } catch (IllegalAccessException ex) {
            throw new IllegalStateException("Could not scan shared fields", ex);
        }
    }

    private byte[] tombstoneHash(Field field) throws IllegalAccessException {
        byte[] value = tombstoneHashes.get(field.getName());
        return value == null ? hash(field.get(this)) : value;
    }

    private void assignRemote(Field field, Object value, boolean deletion) {
        try {
            if (!deletion) {
                field.set(this, value);
                remoteTombstones.remove(field.getName());
                tombstoneHashes.remove(field.getName());
                return;
            }
            if (field.getType().isAssignableFrom(DELETE.getClass())) {
                field.set(this, DELETE);
                remoteTombstones.remove(field.getName());
                tombstoneHashes.remove(field.getName());
            } else {
                Object empty = defaultValue(field.getType());
                field.set(this, empty);
                remoteTombstones.add(field.getName());
                tombstoneHashes.put(field.getName(), hash(empty));
            }
        } catch (IllegalAccessException | IllegalArgumentException ex) {
            throw new IllegalStateException("Remote value is incompatible with field " + field.getName(), ex);
        }
    }

    private static Object defaultValue(Class<?> type) {
        if (!type.isPrimitive()) return null;
        if (type == boolean.class) return false;
        if (type == char.class) return '\0';
        if (type == byte.class) return (byte) 0;
        if (type == short.class) return (short) 0;
        if (type == int.class) return 0;
        if (type == long.class) return 0L;
        if (type == float.class) return 0.0f;
        if (type == double.class) return 0.0d;
        return null;
    }

    private static byte[] hash(Object value) {
        try {
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            return digest.digest(encoded(value));
        } catch (NoSuchAlgorithmException ex) {
            throw new IllegalStateException("Could not hash shared value", ex);
        }
    }

    private static void blob(DataOutputStream out, byte tag, byte[] value) throws IOException {
        out.writeByte(tag);
        out.writeInt(value.length);
        out.write(value);
    }

    private static void encode(DataOutputStream out, Object value) throws IOException {
        if (value == null) out.writeByte(0);
        else if (value instanceof Boolean v) { out.writeByte(1); out.writeBoolean(v); }
        else if (value instanceof Byte || value instanceof Short || value instanceof Integer || value instanceof Long) {
            out.writeByte(2); out.writeLong(((Number) value).longValue());
        } else if (value instanceof Float || value instanceof Double) {
            out.writeByte(3); out.writeLong(Double.doubleToLongBits(((Number) value).doubleValue()));
        } else if (value instanceof String v) {
            blob(out, (byte) 4, v.getBytes(java.nio.charset.StandardCharsets.UTF_8));
        } else if (value instanceof byte[] v) blob(out, (byte) 5, v);
        else if (value instanceof Map<?, ?> v) {
            out.writeByte(6);
            Map<String, Object> sorted = new TreeMap<>();
            for (Map.Entry<?, ?> item : v.entrySet()) {
                if (!(item.getKey() instanceof String key))
                    throw new IllegalArgumentException("shared map keys must be strings");
                sorted.put(key, item.getValue());
            }
            out.writeInt(sorted.size());
            for (Map.Entry<String, Object> item : sorted.entrySet()) {
                blob(out, (byte) 4, item.getKey().getBytes(java.nio.charset.StandardCharsets.UTF_8));
                encode(out, item.getValue());
            }
        } else if (value instanceof List<?> v) {
            out.writeByte(7); out.writeInt(v.size());
            for (Object item : v) encode(out, item);
        } else if (value instanceof Set<?> v) {
            out.writeByte(8);
            List<byte[]> parts = new ArrayList<>();
            for (Object item : v) parts.add(encoded(item));
            parts.sort(Arrays::compareUnsigned);
            out.writeInt(parts.size());
            for (byte[] part : parts) blob(out, (byte) 9, part);
        } else {
            throw new IllegalArgumentException("unsupported shared field value type: " + value.getClass().getName());
        }
    }

    private static byte[] encoded(Object value) {
        try {
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            try (DataOutputStream out = new DataOutputStream(bytes)) { encode(out, value); }
            return bytes.toByteArray();
        } catch (IOException ex) {
            throw new IllegalStateException(ex);
        }
    }

    private static Map<String, byte[]> hashes(Map<String, Object> values) {
        Map<String, byte[]> result = new HashMap<>();
        for (Map.Entry<String, Object> item : values.entrySet())
            result.put(item.getKey(), hash(item.getValue()));
        return result;
    }

    private Snapshot stableSnapshot() {
        Map<String, Object> values = null;
        long after = 0;
        for (int attempt = 0; attempt < 4; attempt++) {
            long before = nativeRevision(handle);
            @SuppressWarnings("unchecked")
            Map<String, Object> current = (Map<String, Object>) nativeSnapshot(handle);
            values = current;
            after = nativeRevision(handle);
            if (before == after) break;
        }
        return new Snapshot(after, values == null ? Map.of() : values);
    }

    private void updateLoop() {
        while (!stop.get()) {
            try {
                syncOnce();
                syncFailure = null;
            } catch (Throwable ex) {
                syncFailure = ex;
            }
            try {
                Thread.sleep(pollMillis);
            } catch (InterruptedException ex) {
                if (stop.get()) break;
            }
        }
    }

    private void syncOnce() {
        synchronized (syncLock) {
            if (closed || !started) return;
            Map<String, Object> current = scanFields();
            Map<String, byte[]> currentHashes = hashes(current);
            Set<String> locallyDirty = new HashSet<>();
            for (String key : union(hashTable.keySet(), currentHashes.keySet())) {
                if (!Arrays.equals(hashTable.get(key), currentHashes.get(key)))
                    locallyDirty.add(key);
            }

            long observedRevision = nativeRevision(handle);
            if (observedRevision > revision) {
                Snapshot remote = stableSnapshot();
                if (remote.revision > revision) {
                    for (String key : union(union(hashTable.keySet(), currentHashes.keySet()), remote.values.keySet())) {
                        if (locallyDirty.contains(key)) continue;
                        Field field = appFields.get(key);
                        if (field == null) continue;
                        if (remote.values.containsKey(key))
                            assignRemote(field, remote.values.get(key), false);
                        else
                            assignRemote(field, null, true);
                    }
                    hashTable.clear();
                    hashTable.putAll(hashes(remote.values));
                    revision = remote.revision;
                    current = scanFields();
                    currentHashes = hashes(current);
                }
            }

            for (String key : union(hashTable.keySet(), currentHashes.keySet())) {
                if (!currentHashes.containsKey(key)) {
                    if (hashTable.containsKey(key)) {
                        nativeDelete(handle, key);
                        hashTable.remove(key);
                    }
                } else if (!Arrays.equals(hashTable.get(key), currentHashes.get(key))) {
                    nativeWrite(handle, key, current.get(key));
                    hashTable.put(key, currentHashes.get(key));
                }
            }
        }
    }

    @SafeVarargs
    private static <T> Set<T> union(Collection<T>... groups) {
        Set<T> result = new HashSet<>();
        for (Collection<T> group : groups) result.addAll(group);
        return result;
    }

    @Override public void close() {
        Thread thread;
        synchronized (syncLock) {
            if (closed) return;
            stop.set(true);
            thread = updater;
        }
        if (thread != null && thread.isAlive() && thread != Thread.currentThread()) {
            thread.interrupt();
            try { thread.join(); }
            catch (InterruptedException ex) { Thread.currentThread().interrupt(); }
        }
        synchronized (syncLock) {
            try {
                if (started) syncOnce();
            } finally {
                closed = true;
                long old = handle;
                handle = 0;
                if (old != 0) nativeClose(old);
            }
        }
    }

    private record Snapshot(long revision, Map<String, Object> values) {}

    private static native long nativeCreate(long clientHandle);
    private static native void nativeWrite(long handle, String key, Object value);
    private static native void nativeDelete(long handle, String key);
    private static native long nativeRevision(long handle);
    private static native Object nativeSnapshot(long handle);
    private static native void nativeClose(long handle);
}
