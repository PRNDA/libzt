package com.example.joseph.example_app;

import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.os.Environment;
import android.util.Log;
import java.net.InetSocketAddress;
import java.net.*;
import android.content.Context;
import android.os.Handler;

import ZeroTier.SDK;

public class MainActivity extends AppCompatActivity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        final SDK zt = new SDK();
        final String homeDir = getApplicationContext().getFilesDir() + "/zerotier";

        // Service thread
        new Thread(new Runnable() {
            public void run() {
                // Calls to JNI code
                zt.zt_start_service(homeDir);
            }
        }).start();

        while(!zt.zt_running()) { }
        zt.zt_join_network("XXXXXXXXXXXXXXXX");

        // Create ZeroTier socket
        int sock = zt.zt_socket(zt.AF_INET, zt.SOCK_STREAM, 0);

        try {
            Thread.sleep(10000);
        }
        catch(java.lang.InterruptedException e) { }

        // Connect to remote host
        Log.d("","zt_connect()\n");
        int err = zt.zt_connect(sock, "10.9.9.203", 8080);

        // Set up example proxy connection to SDK proxy server
        /*
        Log.d("ZTSDK-InJavaland", "Setting up connection to SDK proxy server");
        Socket s = new Socket();
        SocketAddress proxyAddr = new InetSocketAddress("0.0.0.0", 1337);
        Proxy proxy = new Proxy(Proxy.Type.SOCKS, proxyAddr);
        */
    }
}