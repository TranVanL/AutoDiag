package com.vdiag;

import android.os.Bundle;
import android.util.Log;
import android.widget.Button;
import android.widget.TextView;
import android.view.View;

import androidx.activity.EdgeToEdge;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.graphics.Insets;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import com.vdiag.DiagPropertyEvent;
import com.vdiag.sdk.DiagClient;
import com.vdiag.sdk.DiagListener;
import com.vdiag.sdk.DiagProperty;
import com.vdiag.ui.ResultAdapter;
import com.vdiag.ui.ResultItem;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.concurrent.Executor;

public class MainActivity extends AppCompatActivity implements DiagListener {
    private static final String TAG = "MainActivity";

    private static final SimpleDateFormat TIME_FORMAT =
            new SimpleDateFormat("HH:mm:ss", Locale.US);

    private DiagClient mDiagClient;

    private TextView mStatusText;

    private TextView mSocLiveValue;

    private TextView mRpmLiveValue;

    private View mStatusDot;

    private Button mGetVinButton;

    private Button mGetSwVersionButton;

    private Button mGetSOC;

    private Button mGetRPM;

    private Button mGetDtcList;

    private Button mClearDtc;

    private Button mGetDtcSnapshot;

    private ResultAdapter mResultAdapter;

    private DiagClient.SubscriptionToken mSocToken;

    private DiagClient.SubscriptionToken mRpmToken;

    private final Executor mUiExecutor = command -> runOnUiThread(command);


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

        mStatusText = findViewById(R.id.status_text);
        mSocLiveValue = findViewById(R.id.live_soc_value);
        mRpmLiveValue = findViewById(R.id.live_rpm_value);
        mStatusDot = findViewById(R.id.status_dot);
        mGetVinButton = findViewById(R.id.btn_get_vin);
        mGetSwVersionButton = findViewById(R.id.btn_get_sw_version);
        mGetSOC = findViewById(R.id.btn_get_soc);
        mGetRPM = findViewById(R.id.btn_get_rpm);
        mGetDtcList = findViewById(R.id.btn_get_dtc_list);
        mClearDtc = findViewById(R.id.btn_clear_dtc);
        mGetDtcSnapshot = findViewById(R.id.btn_get_dtc_snapshot);
        RecyclerView resultsList = findViewById(R.id.results_list);

        mResultAdapter = new ResultAdapter();
        resultsList.setLayoutManager(new LinearLayoutManager(this));
        resultsList.setAdapter(mResultAdapter);

        setButtonsEnabled(false);
        setStatusConnecting();

        mGetVinButton.setOnClickListener(v -> requestProperty(DiagProperty.VIN));
        mGetSwVersionButton.setOnClickListener(v -> requestProperty(DiagProperty.SW_VERSION));
        mGetSOC.setOnClickListener(view -> requestProperty(DiagProperty.BATTERY_SOC));
        mGetRPM.setOnClickListener(view -> requestProperty(DiagProperty.RPM));
        mGetDtcList.setOnClickListener(v -> requestProperty(DiagProperty.DTC_LIST));
        mClearDtc.setOnClickListener(v -> requestProperty(DiagProperty.DTC_CLEAR));
        mGetDtcSnapshot.setOnClickListener(v -> requestDtcSnapshot());

        mDiagClient = DiagClient.create(this);
        mDiagClient.setListener(this);
        mDiagClient.setConnectionListener((connected, message) -> {
            if (connected) {
                setStatusConnected(message);
                setButtonsEnabled(true);
                startLiveSubscriptions();
            } else {
                stopLiveSubscriptions();
                setStatusConnecting(message);
                setButtonsEnabled(false);
            }
        });
        appendResult("System", getString(R.string.diag_waiting), -1L);
    }

    private void requestProperty(DiagProperty property) {
        if (mDiagClient == null) {
            Log.w(TAG, "DiagClient is null");
            return;
        }

        if (!mDiagClient.isConnected()) {
            setStatusConnecting();
        }

        int requestId = mDiagClient.getProperty(property);
        Log.i(TAG, "Requested property=" + property.name() + " requestId=" + requestId);
    }

    @Override
    protected void onStart() {
        super.onStart();
        Log.i(TAG, "Start Main Activity");
        if (mDiagClient != null && mDiagClient.isConnected()) {
            setStatusConnected(getString(R.string.diag_status_connected));
            setButtonsEnabled(true);
            startLiveSubscriptions();
        }
    }

    @Override
    protected void onStop() {
        stopLiveSubscriptions();
        super.onStop();
        Log.i(TAG, "onStop");
    }

    @Override
    protected void onDestroy() {
        stopLiveSubscriptions();
        if (mDiagClient != null) {
            mDiagClient.close();
            mDiagClient = null;
        }
        super.onDestroy();
    }

    @Override
    public void onPropertyReceived(DiagProperty property, String value, long latencyUs, int requestId) {
        Log.i(TAG, "SDK result reqId=" + requestId + " prop=" + property.name() + " value=" + value);
        setStatusConnected();
        setButtonsEnabled(true);
        appendResult(property.getDisplayName(), value, latencyUs);
    }

    @Override
    public void onError(DiagProperty property, int code, String message, int requestId) {
        String propertyName = property != null ? property.getDisplayName() : "SDK";
        Log.e(TAG, "SDK error reqId=" + requestId + " prop=" + propertyName + " code=" + code + " msg=" + message);
        if (code == DiagClient.ERR_NOT_CONNECTED || code == DiagClient.ERR_BIND_FAILED) {
            setStatusConnecting(message);
            setButtonsEnabled(false);
        }
        appendResult(propertyName, "Error " + code + ": " + message, -1L);
    }

    private void appendResult(String property, String value, long latencyUs) {
        String timestamp = TIME_FORMAT.format(new Date());
        String latency = latencyUs >= 0
                ? String.format(Locale.US, "Latency %.3f ms", latencyUs / 1000.0)
                : "Latency unavailable";
        mResultAdapter.prepend(new ResultItem(timestamp, property, value, latency));
    }

    private void startLiveSubscriptions() {
        if (mDiagClient == null || !mDiagClient.isConnected()) {
            return;
        }

        if (mSocToken == null || !mSocToken.isActive()) {
            try {
                mSocToken = mDiagClient.subscribeProperty(
                        DiagProperty.BATTERY_SOC,
                        1.0f,
                        mUiExecutor,
                        new DiagClient.PropertySubscriptionCallback() {
                            @Override
                            public void onPropertyChanged(DiagProperty property, DiagPropertyEvent event) {
                                String val = event != null && event.stringValue != null
                                        ? event.stringValue
                                        : String.valueOf(event != null ? event.intValue : 0);
                                mSocLiveValue.setText(val + " %");
                            }

                            @Override
                            public void onPropertyError(DiagProperty property, int areaId, int errorCode) {
                                mSocLiveValue.setText("-- %");
                            }
                        }
                );
            } catch (RuntimeException e) {
                Log.e(TAG, "SOC subscribe failed", e);
            }
        }

        if (mRpmToken == null || !mRpmToken.isActive()) {
            try {
                mRpmToken = mDiagClient.subscribeProperty(
                        DiagProperty.RPM,
                        0f,
                        mUiExecutor,
                        new DiagClient.PropertySubscriptionCallback() {
                            @Override
                            public void onPropertyChanged(DiagProperty property, DiagPropertyEvent event) {
                                String val = event != null && event.stringValue != null
                                        ? event.stringValue
                                        : String.valueOf(event != null ? event.intValue : 0);
                                mRpmLiveValue.setText(val + " rpm");
                            }

                            @Override
                            public void onPropertyError(DiagProperty property, int areaId, int errorCode) {
                                mRpmLiveValue.setText("-- rpm");
                            }
                        }
                );
            } catch (RuntimeException e) {
                Log.e(TAG, "RPM subscribe failed", e);
            }
        }
    }

    private void stopLiveSubscriptions() {
        if (mSocToken != null) {
            mSocToken.unsubscribe();
            mSocToken = null;
        }
        if (mRpmToken != null) {
            mRpmToken.unsubscribe();
            mRpmToken = null;
        }
    }

    private void setButtonsEnabled(boolean enabled) {
        mGetVinButton.setEnabled(enabled);
        mGetSwVersionButton.setEnabled(enabled);
        mGetSOC.setEnabled(enabled);
        mGetRPM.setEnabled(enabled);
        mGetDtcList.setEnabled(enabled);
        mClearDtc.setEnabled(enabled);
        mGetDtcSnapshot.setEnabled(enabled);
    }

    private void setStatusConnected(String message) {
        mStatusText.setText(message);
        mStatusDot.setBackgroundTintList(getColorStateList(R.color.diag_accent));
    }

    private void setStatusConnected() {
        setStatusConnected(getString(R.string.diag_status_connected));
    }

    private void setStatusConnecting() {
        setStatusConnecting(getString(R.string.diag_status_connecting));
    }

    private void setStatusConnecting(String message) {
        mStatusText.setText(message);
        mStatusDot.setBackgroundTintList(getColorStateList(R.color.diag_warning));
    }

    private void requestDtcSnapshot() {
        if (mDiagClient == null) {
            Log.w(TAG, "DiagClient is null");
            return;
        }
        if (!mDiagClient.isConnected()) {
            setStatusConnecting();
            return;
        }

        new Thread(() -> {
            String snapshot = mDiagClient.getDtcSnapshotShared();
            runOnUiThread(() -> {
                if (snapshot != null) {
                    appendResult("DTC Snapshot (ASHMEM)", snapshot, -1L);
                } else {
                    appendResult("DTC Snapshot (ASHMEM)", "Failed to load", -1L);
                }
            });
        }).start();
    }
}