package com.snesbt.receiver.service;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

/** Inicia o serviço no boot (BOOT_COMPLETED é isento das restrições de FGS em segundo plano). */
public final class BootReceiver extends BroadcastReceiver {
    @Override public void onReceive(Context context, Intent intent) {
        if (!Intent.ACTION_BOOT_COMPLETED.equals(intent.getAction())) return;
        Log.i("SnesBtReceiver", "BOOT_COMPLETED: iniciando ControllerService");
        ControllerService.start(context);
    }
}
