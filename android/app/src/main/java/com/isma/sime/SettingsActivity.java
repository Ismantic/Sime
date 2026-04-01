package com.isma.sime;

import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.os.Bundle;
import android.provider.Settings;
import android.view.Gravity;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.TextView;

public class SettingsActivity extends Activity {

    public static final String PREFS_NAME = "sime_prefs";
    public static final String KEY_LAYOUT_MODE = "layout_mode";
    public static final int MODE_QWERTY = 0;
    public static final int MODE_T9 = 1;

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
        hint.setText("\n1. 点击「启用输入法」，在列表中开启是语拼音\n2. 点击「选择输入法」，切换到是语拼音\n");
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

        // Keyboard layout setting
        TextView layoutTitle = new TextView(this);
        layoutTitle.setText("\n键盘布局");
        layoutTitle.setTextSize(20);
        layoutTitle.setTextColor(Color.BLACK);
        LinearLayout.LayoutParams titleParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT
        );
        titleParams.topMargin = 48;
        layout.addView(layoutTitle, titleParams);

        SharedPreferences prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE);
        int currentMode = prefs.getInt(KEY_LAYOUT_MODE, MODE_QWERTY);

        RadioGroup radioGroup = new RadioGroup(this);
        radioGroup.setOrientation(RadioGroup.VERTICAL);

        RadioButton rbQwerty = new RadioButton(this);
        rbQwerty.setText("全键盘（中文全键盘，英文全键盘）");
        rbQwerty.setTextSize(16);
        rbQwerty.setId(MODE_QWERTY);
        rbQwerty.setPadding(0, 16, 0, 16);
        radioGroup.addView(rbQwerty);

        RadioButton rbT9 = new RadioButton(this);
        rbT9.setText("九宫格（中文九宫格，英文全键盘）");
        rbT9.setTextSize(16);
        rbT9.setId(MODE_T9);
        rbT9.setPadding(0, 16, 0, 16);
        radioGroup.addView(rbT9);

        radioGroup.check(currentMode);
        radioGroup.setOnCheckedChangeListener((group, checkedId) -> {
            prefs.edit().putInt(KEY_LAYOUT_MODE, checkedId).apply();
        });

        LinearLayout.LayoutParams rgParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT
        );
        rgParams.topMargin = 16;
        layout.addView(radioGroup, rgParams);

        setContentView(layout);
    }
}
