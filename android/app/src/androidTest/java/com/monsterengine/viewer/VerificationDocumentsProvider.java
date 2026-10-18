package com.monsterengine.viewer;

import android.database.Cursor;
import android.database.MatrixCursor;
import android.os.CancellationSignal;
import android.os.Binder;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract.Document;
import android.provider.DocumentsContract;
import android.provider.DocumentsContract.Root;
import android.provider.DocumentsProvider;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;

/** Test APK only; plain Java so its standalone provider needs no target classloader. */
public class VerificationDocumentsProvider extends DocumentsProvider {
    @Override public boolean onCreate() { return true; }
    /** Test setup only: grant from the URI owner's process, never impersonate an
     * ungranted target URI. Caller UID must actually own the fixed target package. */
    @Override public Bundle call(String method, String arg, Bundle extras) {
        if (!"grant-test-document".equals(method)) return super.call(method, arg, extras);
        final String target = "com.monsterengine.viewer.debug";
        String[] packages = getContext().getPackageManager().getPackagesForUid(Binder.getCallingUid());
        boolean allowed = false;
        if (packages != null) for (String name : packages) if (target.equals(name)) allowed = true;
        if (!allowed) throw new SecurityException("Only the verification target may request fixture grants");
        if (!"small.ply".equals(arg) && !"slow.ply".equals(arg) && !"malformed.ply".equals(arg)) throw new SecurityException("Not a generated test fixture");
        long identity = Binder.clearCallingIdentity();
        try {
            getContext().grantUriPermission(target, DocumentsContract.buildDocumentUri(
                "com.monsterengine.viewer.debug.test.documents", arg), android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION);
            Bundle result = new Bundle(); result.putBoolean("granted", true); return result;
        } finally { Binder.restoreCallingIdentity(identity); }
    }
    @Override public Cursor queryRoots(String[] projection) {
        MatrixCursor cursor = new MatrixCursor(projection != null ? projection : new String[]{
            Root.COLUMN_ROOT_ID, Root.COLUMN_DOCUMENT_ID, Root.COLUMN_TITLE, Root.COLUMN_FLAGS, Root.COLUMN_MIME_TYPES});
        Object[] row = new Object[cursor.getColumnCount()];
        String[] columns = cursor.getColumnNames();
        for (int i = 0; i < columns.length; ++i) {
            switch (columns[i]) {
                case Root.COLUMN_ROOT_ID: row[i] = "verification"; break;
                case Root.COLUMN_DOCUMENT_ID: row[i] = "root"; break;
                case Root.COLUMN_TITLE: row[i] = "Monster Verification"; break;
                case Root.COLUMN_FLAGS: row[i] = Root.FLAG_LOCAL_ONLY; break;
                case Root.COLUMN_MIME_TYPES: row[i] = "*/*"; break;
                default: break;
            }
        }
        cursor.addRow(row); return cursor;
    }
    @Override public Cursor queryDocument(String id, String[] projection) throws FileNotFoundException {
        MatrixCursor cursor = cursor(projection); addDocument(cursor, id); return cursor;
    }
    @Override public Cursor queryChildDocuments(String parent, String[] projection, String sortOrder) throws FileNotFoundException {
        if (!"root".equals(parent)) throw new FileNotFoundException(parent);
        MatrixCursor cursor = cursor(projection);
        for (String id : new String[]{"small.ply", "slow.ply", "malformed.ply"}) addDocument(cursor, id);
        File[] files = fixtureDirectory().listFiles();
        if (files != null) for (File file : files) if (file.isFile() && file.getName().matches("[a-zA-Z0-9_.-]+")) addDocument(cursor, "fixture-" + file.getName());
        return cursor;
    }
    @Override public ParcelFileDescriptor openDocument(String id, String mode, CancellationSignal signal) throws FileNotFoundException {
        if (!"r".equals(mode)) throw new FileNotFoundException("Read-only verification fixture");
        if (id.startsWith("fixture-")) return ParcelFileDescriptor.open(fixture(id), ParcelFileDescriptor.MODE_READ_ONLY);
        if (!id.equals("small.ply") && !id.equals("slow.ply") && !id.equals("malformed.ply")) throw new FileNotFoundException(id);
        try {
            ParcelFileDescriptor[] pipe = ParcelFileDescriptor.createPipe();
            new Thread(() -> {
                try (OutputStream output = new java.io.BufferedOutputStream(new ParcelFileDescriptor.AutoCloseOutputStream(pipe[1]), 65536)) {
                    if (id.equals("malformed.ply")) { output.write("invalid document".getBytes(StandardCharsets.US_ASCII)); return; }
                    int count = id.equals("slow.ply") ? 160000 : 1;
                    output.write(header(count).getBytes(StandardCharsets.US_ASCII));
                    byte[] row = "0 0 0 0 0 0 1 -3 -3 -3 1 0 0 0\n".getBytes(StandardCharsets.US_ASCII);
                    for (int i = 0; i < count; ++i) {
                        if (signal != null && signal.isCanceled()) throw new java.io.IOException("Cancelled");
                        output.write(row);
                        if (count > 1 && i % 2048 == 2047) { output.flush(); Thread.sleep(200); }
                    }
                } catch (Exception error) { try { pipe[1].close(); } catch (Exception ignored) {} }
            }, "MonsterVerificationDocument").start();
            return pipe[0];
        } catch (java.io.IOException error) { throw new FileNotFoundException(error.toString()); }
    }
    private File fixtureDirectory() { File directory = new File(getContext().getFilesDir(), "fixtures"); if (!directory.mkdirs() && !directory.isDirectory()) throw new IllegalStateException("Cannot create fixture directory"); return directory; }
    private File fixture(String id) throws FileNotFoundException {
        String name = id.substring("fixture-".length());
        if (!name.matches("[a-zA-Z0-9_.-]+")) throw new FileNotFoundException(name);
        File file = new File(fixtureDirectory(), name); if (!file.isFile()) throw new FileNotFoundException(name); return file;
    }
    private MatrixCursor cursor(String[] projection) { return new MatrixCursor(projection != null ? projection : new String[]{
        Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME, Document.COLUMN_MIME_TYPE, Document.COLUMN_FLAGS, Document.COLUMN_SIZE}); }
    private void addDocument(MatrixCursor cursor, String id) throws FileNotFoundException {
        Object[] row = new Object[cursor.getColumnCount()]; String[] columns = cursor.getColumnNames();
        for (int i = 0; i < columns.length; ++i) {
            switch (columns[i]) {
                case Document.COLUMN_DOCUMENT_ID: row[i] = id; break;
                case Document.COLUMN_DISPLAY_NAME: row[i] = id.equals("root") ? "Monster Verification" : id.replaceFirst("^fixture-", ""); break;
                case Document.COLUMN_MIME_TYPE: row[i] = id.equals("root") ? Document.MIME_TYPE_DIR : "application/octet-stream"; break;
                case Document.COLUMN_FLAGS: row[i] = 0; break;
                case Document.COLUMN_SIZE: row[i] = id.startsWith("fixture-") ? fixture(id).length() : id.equals("slow.ply") ? 4960500L : 512L; break;
                default: break;
            }
        }
        cursor.addRow(row);
    }
    public static String header(int count) {
        StringBuilder out = new StringBuilder("ply\nformat ascii 1.0\nelement vertex " + count + "\n");
        for (String name : new String[]{"x", "y", "z", "f_dc_0", "f_dc_1", "f_dc_2", "opacity", "scale_0", "scale_1", "scale_2", "rot_0", "rot_1", "rot_2", "rot_3"}) out.append("property float ").append(name).append('\n');
        return out.append("end_header\n").toString();
    }
}
