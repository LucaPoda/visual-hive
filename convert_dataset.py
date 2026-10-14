import pandas as pd
import os

def converti_dataset(input_file='knn_dataset_clean.csv', output_file='knn_dataset_4f.csv'):
    if not os.path.exists(input_file):
        print(f"File {input_file} non trovato!")
        return

    # Nomi originali delle 8 feature + Label
    colonne_orig = ['SubBass', 'Bass', 'LowMid', 'Mid', 'HighMid', 'High', 'RMS', 'DeltaIntensity', 'Label']
    
    # Carica il dataset pulito
    df = pd.read_csv(input_file, names=colonne_orig)
    
    # Seleziona solo le 4 feature richieste + Label
    colonne_nuove = ['SubBass', 'Bass', 'High', 'RMS', 'Label']
    df_nuovo = df[colonne_nuove]
    
    # Salva il nuovo CSV per il C++
    df_nuovo.to_csv(output_file, index=False, header=False)
    print(f"Dataset convertito! Salvato in: {output_file}")
    print(f"Nuove dimensioni: {df_nuovo.shape[1]-1} feature, {len(df_nuovo)} sample.")

if __name__ == "__main__":
    converti_dataset()