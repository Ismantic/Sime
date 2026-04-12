package com.semantic.sime;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.provider.Settings;
import android.view.Gravity;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

/**
 * Single-screen settings UI: two buttons for enabling and switching to the
 * IME. The Chinese keyboard layout picker lives in the inline settings
 * panel on the candidates bar instead.
 */
public class SettingsActivity extends Activity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setFitsSystemWindows(true);
        root.setPadding(dp(32), dp(80), dp(32), dp(32));

        TextView title = new TextView(this);
        title.setText("是语输入法");
        title.setTextSize(28f);
        title.setGravity(Gravity.CENTER);
        root.addView(title);

        LinearLayout.LayoutParams btnLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        btnLp.topMargin = dp(16);

        Button btnEnable = new Button(this);
        btnEnable.setText("输入法启用");
        btnEnable.setOnClickListener(v ->
                startActivity(new Intent(Settings.ACTION_INPUT_METHOD_SETTINGS)));
        root.addView(btnEnable, btnLp);

        Button btnSwitch = new Button(this);
        btnSwitch.setText("输入法选择");
        btnSwitch.setOnClickListener(v -> {
            InputMethodManager imm = (InputMethodManager)
                    getSystemService(INPUT_METHOD_SERVICE);
            if (imm != null) imm.showInputMethodPicker();
        });
        root.addView(btnSwitch, btnLp);

        setContentView(root);
    }

    private int dp(int v) {
        return (int) (v * getResources().getDisplayMetrics().density);
    }
}
