package com.vdiag.ui;

import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.recyclerview.widget.RecyclerView;

import com.vdiag.R;

import java.util.ArrayList;
import java.util.List;

public final class ResultAdapter extends RecyclerView.Adapter<ResultAdapter.ResultViewHolder> {
    private final List<ResultItem> items = new ArrayList<>();

    public void prepend(ResultItem item) {
        items.add(0, item);
        notifyItemInserted(0);
    }

    @NonNull
    @Override
    public ResultViewHolder onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
        View view = LayoutInflater.from(parent.getContext()).inflate(R.layout.item_result, parent, false);
        return new ResultViewHolder(view);
    }

    @Override
    public void onBindViewHolder(@NonNull ResultViewHolder holder, int position) {
        ResultItem item = items.get(position);
        holder.propertyView.setText(item.getProperty());
        holder.timestampView.setText(item.getTimestamp());
        holder.valueView.setText(item.getValue());
        holder.latencyView.setText(item.getLatency());
    }

    @Override
    public int getItemCount() {
        return items.size();
    }

    static final class ResultViewHolder extends RecyclerView.ViewHolder {
        private final TextView propertyView;
        private final TextView timestampView;
        private final TextView valueView;
        private final TextView latencyView;

        ResultViewHolder(@NonNull View itemView) {
            super(itemView);
            propertyView = itemView.findViewById(R.id.item_property);
            timestampView = itemView.findViewById(R.id.item_timestamp);
            valueView = itemView.findViewById(R.id.item_value);
            latencyView = itemView.findViewById(R.id.item_latency);
        }
    }
}