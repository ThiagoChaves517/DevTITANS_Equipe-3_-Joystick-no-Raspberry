package com.snesbt.receiver;

import android.Manifest;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;

import com.snesbt.receiver.service.ControllerService;

/**
 * Tela opcional de diagnóstico. O sistema não depende dela: o {@link ControllerService} roda
 * headless e pode ser iniciado por ADB ou no boot. Aqui apenas pedimos as permissões em tempo de
 * execução (alternativa ao pm grant), iniciamos o serviço e exibimos seu resumo.
 */
public class MainActivity extends AppCompatActivity {
    private static final String[] PERMISSIONS = {Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT};
    private static final int REQUEST = 1;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private TextView status;

    private final Runnable refresh = new Runnable() {
        @Override public void run() {
            status.setText(ControllerService.summary().replace(" | ", "\n"));
            handler.postDelayed(this, 500);
        }
    };

    @Override protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        status = findViewById(R.id.statusText);
        if (granted()) ControllerService.start(this);
        else requestPermissions(PERMISSIONS, REQUEST);
    }

    @Override public void onRequestPermissionsResult(int code, @NonNull String[] p, @NonNull int[] results) {
        super.onRequestPermissionsResult(code, p, results);
        if (code == REQUEST && granted()) ControllerService.start(this);
        else status.setText("Permissões Bluetooth negadas");
    }

    @Override protected void onResume() {
        super.onResume();
        handler.post(refresh);
    }

    @Override protected void onPause() {
        handler.removeCallbacks(refresh);
        super.onPause();
    }

    private boolean granted() {
        for (String p : PERMISSIONS) if (checkSelfPermission(p) != PackageManager.PERMISSION_GRANTED) return false;
        return true;
    }
}
