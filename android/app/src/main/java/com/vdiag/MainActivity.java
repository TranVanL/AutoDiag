package com.vdiag;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Bundle;
import android.os.IBinder;
import android.os.RemoteException;
import android.util.Log;
import android.widget.Button;
import android.widget.TextView;
import android.widget.Toast;

import androidx.activity.EdgeToEdge;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.graphics.Insets;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowInsetsCompat;

public class MainActivity extends AppCompatActivity {
    private static final String TAG = "MainActivity";

    private IDiagCarService mDiagService;

    private TextView mResultText;

    private Button mGetVinButton;

    private Button mGetSwVersionButton;

    private boolean mIsBound = false;


    private final ServiceConnection mConnection = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName componentName, IBinder iBinder) {
            Log.i(TAG, "onServiceConnected");
            mDiagService = IDiagCarService.Stub.asInterface(iBinder);
            try {
                mDiagService.registerCallback(mCallback);
                Log.i(TAG, "Callback registered to service");
            } catch (RemoteException e) {
                Log.e(TAG, "Failed to register callback", e);
            }
            Toast.makeText(MainActivity.this, "Service connected !!!", Toast.LENGTH_LONG).show();
            mGetVinButton.setEnabled(true);
            mGetSwVersionButton.setEnabled(true);
        }

        @Override
        public void onServiceDisconnected(ComponentName componentName) {
            Log.i(TAG, "onServiceDisconnected");
            mDiagService = null;
            mIsBound = false;
            mGetVinButton.setEnabled(false);
            mGetSwVersionButton.setEnabled(false);
            Toast.makeText(MainActivity.this, "Service disconnected !!!", Toast.LENGTH_LONG).show();
        }
    };

    private final IDiagCallback mCallback = new IDiagCallback.Stub() {
        @Override
        public void onResult(int requestId, String value, long latencyUs) {
            Log.i(TAG, "onResult - callback from service to client !!!");
            runOnUiThread(() -> {
                mResultText.setText("RequestID : " + requestId + "\nValue : " + value + "\nLatency : " + latencyUs );
                Toast.makeText(MainActivity.this, "OnResult - Callback ", Toast.LENGTH_SHORT).show();
            });
        }

        @Override
        public void onError(int requestId, int errorCode, String errorMsg) {
            Log.i(TAG, "onError - callback from service to client !!!");
            runOnUiThread(() -> {
                mResultText.setText("RequestID : " + requestId + "\nErrorCode : " + errorCode + "\nerrorMsg : " + errorMsg );
                Toast.makeText(MainActivity.this, "onError - Callback ", Toast.LENGTH_SHORT).show();
            });
        }
    };


    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        EdgeToEdge.enable(this);
        setContentView(R.layout.activity_main);
        ViewCompat.setOnApplyWindowInsetsListener(findViewById(R.id.main), (v, insets) -> {
            Insets systemBars = insets.getInsets(WindowInsetsCompat.Type.systemBars());
            v.setPadding(systemBars.left, systemBars.top, systemBars.right, systemBars.bottom);
            return insets;
        });

        mResultText = findViewById(R.id.result_text);
        mGetVinButton = findViewById(R.id.btn_get_vin);
        mGetSwVersionButton = findViewById(R.id.btn_get_sw_version);

        mGetVinButton.setEnabled(false);
        mGetSwVersionButton.setEnabled(false);

        mGetVinButton.setOnClickListener(v -> requestProperty(0xF190, "VIN"));
        mGetSwVersionButton.setOnClickListener(v -> requestProperty(0xF187 , "SW_VERSION"));
        findViewById(R.id.btn_crash_app).setOnClickListener(v -> {
            Log.w(TAG, "💥 Self-killing client process...");
            android.os.Process.killProcess(android.os.Process.myPid());
        });
    }

    private void requestProperty(int propertyId, String propertyName)  {
        if (mDiagService == null) {
            Log.w(TAG , "Service isn't working !!!");
            return;
        }

        try {
            DiagRequest request = new DiagRequest();
            request.requestId = (int) (System.currentTimeMillis() % 1000);
            request.propertyId = propertyId;

            Log.i(TAG , "Send DiagRequest " + request.requestId + " for " + propertyName + " to Service");
            Toast.makeText(MainActivity.this , "Send request: " + propertyName , Toast.LENGTH_SHORT).show();
            mDiagService.getProperty(request);
        }
        catch (RemoteException e) {
            Log.e(TAG , "Exception Handle !!!");
        }
    }

    @Override
    protected void onStart() {
        super.onStart();
        Log.i(TAG , "Start Main Activity");
        Intent intent = new Intent();
        intent.setComponent(new ComponentName("com.vdiag" , "com.vdiag.service.DiagCarService"));
        bindService(intent, mConnection,Context.BIND_AUTO_CREATE);
        mIsBound = true;
    }

    @Override
    protected void onStop() {
        super.onStop();
        Log.i(TAG, "🔓 onStop — unbindService");
        if (mIsBound) {
            if (mDiagService != null) {
                try {
                    mDiagService.unregisterCallback(mCallback);
                } catch (RemoteException e) {
                    Log.e(TAG, "Failed to unregister callback", e);
                }
            }
            unbindService(mConnection);
            mDiagService = null;
            mIsBound = false;
        }
    }
}