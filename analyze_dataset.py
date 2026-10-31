import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import seaborn as sns
import os
from sklearn.preprocessing import StandardScaler
from sklearn.neighbors import KNeighborsClassifier
from sklearn.metrics import accuracy_score, confusion_matrix

def analyze_knn_full_dataset(file_path='datasets/new_dataset_14_feature.csv'):
    if not os.path.exists(file_path):
        print(f"Errore: Il file {file_path} non esiste.")
        return

    # 1. Caricamento (14 Feature + 1 Label)
    feature_names = [
        'RMS', 'ZCR', 'Crest', 'Flux', 
        'Centroid', 'Spread', 'Rolloff', 
        'SubBass', 'Bass', 'LowMid', 'Mid', 
        'HighMid', 'Presence', 'Brilliance'
    ]
    column_names = feature_names + ['Label']
    
    df = pd.read_csv(file_path, names=column_names)
    
    label_mapping = {
        0: 'EMPTY_PAD', 1: 'LIGHT_RHYTHM', 2: 'VOCAL_AMBIENT', 
        3: 'BUILD_SNARE', 4: 'BUILD_ACID', 5: 'BUILD_MELODIC', 
        6: 'DROP_RAW', 7: 'DROP_MELODIC', 8: 'BRIDGE'
    }
    
    macro_mapping = {
        'EMPTY_PAD': 'LOW', 'LIGHT_RHYTHM': 'LOW', 'VOCAL_AMBIENT': 'LOW',
        'BUILD_SNARE': 'MID', 'BUILD_ACID': 'MID', 'BUILD_MELODIC': 'MID',
        'DROP_RAW': 'HIGH', 'DROP_MELODIC': 'HIGH', 'BRIDGE': 'HIGH'
    }
    
    df['LabelName'] = df['Label'].map(label_mapping)
    print(f"Dataset Totale: {len(df)} campioni.\n")
    label_order = list(label_mapping.values())

    # ==========================================
    # PARTE 1: DIAGRAMMI A VIOLINO (DISTRIBUZIONI)
    # ==========================================
    print("Generazione dei Violin Plot in corso...")
    
    # Prepariamo una griglia 7x2 per le 14 feature
    fig_v, axes_v = plt.subplots(7, 2, figsize=(24, 35))
    axes_v = axes_v.flatten()

    # Creiamo un violin plot per ogni feature
    for idx, feature in enumerate(feature_names):
        sns.violinplot(
            data=df,
            x='LabelName',
            y=feature,
            order=label_order,
            ax=axes_v[idx],
            palette='husl',
            inner='quartile', # Mostra quartili e mediana all'interno del violino
            linewidth=1.2
        )
        axes_v[idx].set_title(f"Distribuzione Feature: {feature}", fontsize=16, fontweight='bold')
        axes_v[idx].set_xlabel('')
        axes_v[idx].set_ylabel('Valore Z-Score Normalizzato')
        axes_v[idx].tick_params(axis='x', rotation=45)
        axes_v[idx].grid(axis='y', linestyle='--', alpha=0.6)

    plt.tight_layout()
    plt.savefig('feature_distributions_violin.png', bbox_inches='tight', dpi=150)
    print("✅ Diagrammi a violino esportati come 'feature_distributions_violin.png'")

    # ==========================================
    # PARTE 2: VALUTAZIONE KNN CLOSED-LOOP
    # ==========================================
    X = df[feature_names]
    y_9 = df['LabelName']
    y_macro = y_9.map(macro_mapping)

    # Standardizzazione
    scaler = StandardScaler()
    X_scaled = scaler.fit_transform(X)

    # Test su K multipli
    k_values = [3, 5, 11, 21]
    
    fig, axes = plt.subplots(2, 2, figsize=(22, 18))
    axes = axes.flatten()

    print("\n--- RISULTATI CLOSED-LOOP K-NEIGHBORS ---")
    for idx, k in enumerate(k_values):
        actual_k = min(k, len(df))
        
        model = KNeighborsClassifier(n_neighbors=actual_k)
        model.fit(X_scaled, y_9)
        
        y_pred_9 = model.predict(X_scaled)
        y_pred_macro = pd.Series(y_pred_9).map(macro_mapping)
        
        acc_9 = accuracy_score(y_9, y_pred_9)
        acc_macro = accuracy_score(y_macro, y_pred_macro)
        
        print(f"KNN (K={actual_k:2d}) | Acc 9-Classi: {acc_9*100:.1f}% | Acc Macro: {acc_macro*100:.1f}%")

        cm = confusion_matrix(y_9, y_pred_9, labels=label_order)
        sns.heatmap(cm, annot=True, fmt='d', cmap='Greens', 
                    xticklabels=label_order, yticklabels=label_order, 
                    cbar=False, ax=axes[idx], square=True)
        
        axes[idx].set_title(f"KNN (K={actual_k})\nMacro Acc (Sui dati visti): {acc_macro*100:.1f}%", fontsize=15, pad=15)
        axes[idx].set_ylabel('True Label')
        axes[idx].set_xlabel('Predicted Label')
        axes[idx].tick_params(axis='x', rotation=45)

    plt.tight_layout()
    plt.savefig('knn_closed_loop_evaluation.png', bbox_inches='tight')
    print("✅ Matrici di confusione esportate come 'knn_closed_loop_evaluation.png'")

if __name__ == "__main__":
    analyze_knn_full_dataset('datasets/new_dataset_14_feature.csv')