package com.isma.sime;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.provider.Settings;
import android.view.Gravity;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

public class SettingsActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(64, 160, 64, 64);
        layout.setGravity(Gravity.CENTER_HORIZONTAL);

        TextView title = new TextView(this);
        title.setText("是语输入法");
        title.setTextSize(28);
        title.setGravity(Gravity.CENTER);
        layout.addView(title);

        TextView hint = new TextView(this);
        hint.setText("\n1. 点击「启用输入法」，在列表中开启是语拼音\n2. 点击��选择输入法」，切换到是语拼音\n");
        hint.setTextSize(16);
        layout.addView(hint);

        LinearLayout.LayoutParams btnParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT
        );
        btnParams.topMargin = 32;

        Button btnEnable = new Button(this);
        btnEnable.setText("启用输入法");
        btnEnable.setTextSize(18);
        btnEnable.setOnClickListener(v -> {
            startActivity(new Intent(Settings.ACTION_INPUT_METHOD_SETTINGS));
        });
        layout.addView(btnEnable, btnParams);

        LinearLayout.LayoutParams btn2Params = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT
        );
        btn2Params.topMargin = 24;

        Button btnSwitch = new Button(this);
        btnSwitch.setText("选择输入法");
        btnSwitch.setTextSize(18);
        btnSwitch.setOnClickListener(v -> {
            InputMethodManager imm = (InputMethodManager)
                getSystemService(INPUT_METHOD_SERVICE);
            if (imm != null) {
                imm.showInputMethodPicker();
            }
        });
        layout.addView(btnSwitch, btn2Params);

        setContentView(layout);
    }
}
