import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import seaborn as sns
import os

def analyze_and_filter_dataset(file_path='knn_dataset.csv', output_path='knn_dataset_clean.csv', iqr_multiplier=1.0):
    if not os.path.exists(file_path):
        print(f"Errore: Il file {file_path} non esiste.")
        return

    # 1. Caricamento del dataset con le 5 feature definitive
    feature_names = ['RMS', 'SubBass', 'Bass', 'HighMid', 'SpectralFlux']
    column_names = feature_names + ['Label']
    
    try:
        df_orig = pd.read_csv(file_path, names=column_names)
    except Exception as e:
        print(f"Errore nella lettura del CSV: {e}")
        return

    label_mapping = {0: 'LOW', 1: 'MID', 2: 'HIGH'}
    df_orig['LabelName'] = df_orig['Label'].map(label_mapping)
    df_orig['Versione'] = 'Originale'

    print(f"--- DATASET ORIGINALE: {len(df_orig)} samples ---")
    print(df_orig['LabelName'].value_counts(), "\n")

    # 2. Filtraggio Outlier (IQR - Interquartile Range)
    df_filtered = pd.DataFrame()
    for label in ['LOW', 'MID', 'HIGH']:
        group = df_orig[df_orig['LabelName'] == label].copy()
        
        # Calcolo del primo (Q1) e terzo (Q3) quartile
        Q1 = group[feature_names].quantile(0.25)
        Q3 = group[feature_names].quantile(0.75)
        IQR = Q3 - Q1
        
        # Definiamo i limiti (iqr_multiplier a 1.0 è molto aggressivo, stringe i cluster)
        lower_bound = Q1 - (iqr_multiplier * IQR)
        upper_bound = Q3 + (iqr_multiplier * IQR)
        
        # Manteniamo solo le righe che rientrano nei limiti per TUTTE le feature
        mask = ((group[feature_names] >= lower_bound) & (group[feature_names] <= upper_bound)).all(axis=1)
        df_filtered = pd.concat([df_filtered, group[mask]])

    df_filtered['Versione'] = 'Filtrato (IQR)'

    print(f"--- DATASET FILTRATO (IQR Mult={iqr_multiplier}): {len(df_filtered)} samples ---")
    print(df_filtered['LabelName'].value_counts(), "\n")
    print(f"Sample rimossi: {len(df_orig) - len(df_filtered)}\n")

    # Salvataggio CSV pulito
    df_to_save = df_filtered[column_names]
    df_to_save.to_csv(output_path, index=False, header=False)
    print(f"✅ Dataset pulito salvato in: '{output_path}'")

    # 3. Visualizzazione Grafica
    df_combined = pd.concat([df_orig, df_filtered], ignore_index=True)
    label_order = ['LOW', 'MID', 'HIGH']
    
    sns.set_theme(style="whitegrid")
    plt.figure(figsize=(20, 4))
    
    for i, feature in enumerate(feature_names, 1):
        plt.subplot(1, 5, i)
        sns.violinplot(
            x='LabelName', 
            y=feature, 
            hue='Versione', 
            data=df_combined, 
            order=label_order, 
            split=True, 
            inner='quartile',
            palette={'Originale': '#bdc3c7', 'Filtrato (IQR)': '#2980b9'}
        )
        plt.title(f'{feature}')
        plt.xlabel('')
        plt.ylabel('Valore' if i == 1 else '')
        if i != 1:
            plt.legend([],[], frameon=False)
        else:
            plt.legend(loc='upper right')

    plt.tight_layout()
    plt.savefig('violin_plot_iqr.png')
    print("Grafico salvato come 'violin_plot_iqr.png'.")
    plt.show()

if __name__ == "__main__":
    analyze_and_filter_dataset('knn_dataset.csv', iqr_multiplier=0.75)