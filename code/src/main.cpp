#include <Arduino.h>

// Définition des broches
const int brocheLed = 2;    // GPIO 2 (souvent la LED intégrée D2)
const int brocheBouton = 4; // GPIO 4 (D4 sur la carte) pour le bouton

void setup() {
  // Configure la broche de la LED en sortie
  pinMode(brocheLed, OUTPUT);
  
  // Configure la broche du bouton en entrée avec pull-up interne
  pinMode(brocheBouton, INPUT_PULLUP);
}

void loop() {
  // Lecture de l'état du bouton
  int etatBouton = digitalRead(brocheBouton);

  // Si le bouton est pressé (le signal passe à LOW à cause du pull-up)
  if (etatBouton == LOW) {
    digitalWrite(brocheLed, HIGH); // Allume la LED
  } else {
    digitalWrite(brocheLed, LOW);  // Éteint la LED
  }
}