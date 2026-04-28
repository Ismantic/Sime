package com.shiyu.sime;

import com.shiyu.sime.ime.theme.Typography;
import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;

import com.shiyu.sime.ime.data.QuickPhraseStore;

/**
 * Full-screen editor for adding or editing a quick phrase. Launched
 * from the IME's picker panel via "+" or per-row edit button. When
 * {@link #EXTRA_EDIT_INDEX} is supplied, the entry at that index is
 * replaced; otherwise a new entry is added.
 */
public class QuickPhraseEditActivity extends Activity {

    public static final String EXTRA_EDIT_INDEX = "edit_index";
    public static final String EXTRA_INITIAL_TEXT = "initial_text";

    private EditText editText;
    private int editIndex = -1;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        editIndex = getIntent().getIntExtra(EXTRA_EDIT_INDEX, -1);
        String initialText = getIntent().getStringExtra(EXTRA_INITIAL_TEXT);
        boolean editing = editIndex >= 0;
        setTitle(editing ? "编辑常用语" : "添加常用语");

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(20), dp(20), dp(20), dp(20));

        TextView label = new TextView(this);
        label.setText(editing ? "修改常用语内容：" : "输入常用语内容：");
        label.setTextSize(Typography.CALLOUT);
        root.addView(label);

        editText = new EditText(this);
        editText.setHint("例如：邮箱、地址、常用回复...");
        editText.setInputType(InputType.TYPE_CLASS_TEXT
                | InputType.TYPE_TEXT_FLAG_MULTI_LINE);
        if (initialText != null) editText.setText(initialText);
        editText.setMinLines(2);
        editText.setGravity(Gravity.TOP | Gravity.START);
        LinearLayout.LayoutParams etLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        etLp.topMargin = dp(8);
        root.addView(editText, etLp);

        LinearLayout buttonRow = new LinearLayout(this);
        buttonRow.setOrientation(LinearLayout.HORIZONTAL);
        buttonRow.setGravity(Gravity.END);
        LinearLayout.LayoutParams brLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        brLp.topMargin = dp(16);
        root.addView(buttonRow, brLp);

        Button cancel = new Button(this);
        cancel.setText("取消");
        cancel.setOnClickListener(v -> finish());
        buttonRow.addView(cancel);

        Button save = new Button(this);
        save.setText("保存");
        LinearLayout.LayoutParams saveLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        saveLp.leftMargin = dp(8);
        save.setLayoutParams(saveLp);
        save.setOnClickListener(v -> doSave());
        buttonRow.addView(save);

        setContentView(root);

        editText.requestFocus();
        getWindow().setSoftInputMode(
                WindowManager.LayoutParams.SOFT_INPUT_STATE_VISIBLE);
    }

    private void doSave() {
        String text = editText.getText().toString().trim();
        if (text.isEmpty()) {
            finish();
            return;
        }
        QuickPhraseStore store = new QuickPhraseStore(this);
        if (editIndex >= 0) {
            store.updateAt(editIndex, text);
        } else {
            store.add(text);
        }
        // Hide the soft keyboard before finishing so the previous
        // activity's IME comes back cleanly.
        InputMethodManager imm = (InputMethodManager)
                getSystemService(INPUT_METHOD_SERVICE);
        if (imm != null && editText != null) {
            imm.hideSoftInputFromWindow(editText.getWindowToken(), 0);
        }
        finish();
    }

    private int dp(int v) {
        return (int) android.util.TypedValue.applyDimension(
                android.util.TypedValue.COMPLEX_UNIT_DIP,
                v, getResources().getDisplayMetrics());
    }
}
