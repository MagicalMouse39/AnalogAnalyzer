#define _NBV ~_BV

#define sbi(reg, bit) (reg |= _BV(bit))
#define cbi(reg, bit) (reg &= _NBV(bit))

#define cli() __asm__ __volatile__ ("cli");
#define sei() __asm__ __volatile__ ("sei");

// Tempo di lettura
#define READ_TIME 120

// Campionamento
int currentReadings;
int maxReadings;
byte currentCol;
byte readData;
byte timeBase;

// Variabile d'appoggio per registrazione
bool isRecording;

// Variabile d'appoggio per il meccanismo SET-RESET del pulsante
bool lockStartBtn;

/* Inizio Dati Comunicazione SPI */

byte spistatus[64];

#define OP_DECODEMODE  9
#define OP_INTENSITY   10
#define OP_SCANLIMIT   11
#define OP_SHUTDOWN    12
#define OP_DISPLAYTEST 15

/* Fine Dati Comunicazione SPI */

void setup() {
  Serial.begin(9600);
  // Imposto tutti i pin da 0 a 7 in OUTPUT
  DDRD = 0xFF;

  // Inizializzo la variabile di stato
  isRecording = false;

  // Inizializzo la variabile per il meccanismo SET-RESET del pulsante
  lockStartBtn = false;

  // Inizializzo la base dei tempi a 0
  timeBase = 0;

  // Imposto solamente il pin 7 (bottone) come input
  cbi(DDRD, DDD7);

  // Inizializzo la matrice led
  ledInit();
}

void loop() {
  // Variabile d'appoggio: true se il pulsante viene premuto in quell'istante
  bool startBtnDown = PIND & _BV(DDD7);

  // Meccanismo SET-RESET per chiamare solo una volta la funzione anche se il pulsante viene tenuto premuto
  if (startBtnDown && !lockStartBtn) {
    // SET
    lockStartBtn = true;
    
    if (isRecording) {
      // Fermo la registrazione dei dati
      stopRecording();
    } else {
      // Inizio della registrazione dei dati
      startRecording();
    }
  } else if (!startBtnDown) {
    // RESET
    lockStartBtn = false;
  }

  if (isRecording) {
    if (currentReadings >= maxReadings) {
      currentReadings = 0;
      ledSetCol(currentCol, byteToLeds(readData));
      currentCol = (currentCol + 1) % 8;
    }
  } else {
    timeBase = readAnalogByte(A5);
    byte row = byteToLeds(timeBase);
    ledSetRow(3, row);
    ledSetRow(4, row);
  }
}

/* Inizio gestione matrice LED */

// Funzione utilitaria per scrivere un byte bit-per-bit su uno specifico pin (pin 6), tickando un clock (pin 4) ad ogni scrittura
// Codifica: MSBFIRST
void spiShiftOut(byte val) {
  for (byte i = 0; i < 8; i++)  {
    if (val & _BV(7 - i)) {
      sbi(PORTD, DDD6);
    } else {
      cbi(PORTD, DDD6);
    }
    
    // Setto e resetto il pin 4, facendo fare un tick al clock
    sbi(PORTD, DDD4);
    cbi(PORTD, DDD4);            
  }
}

void spiSend(volatile byte opcode, volatile byte data) {
  // Il numero di bytes nell'array si calcola facendo 2 * NUM_DISPOSITIVI, essendo che io ho una matrice sola, imposto direttamente il numero a 2 * 1 = 2
  byte spidata[] = { opcode, data };
  
  // Attivo la linea di comunicazione, impostando CHIP SELECT a LOW
  cbi(PORTD, DDD5);

  // Uso la mia versione di shiftOut per scrivere i bit uno alla volta nel pin 6 (DATA IN)
  for(int i = 2; i > 0; --i) {
    spiShiftOut(spidata[i]);
  }

  // Riattivo il CHIP SELECT, facendo leggere al display i dati precedentemente inseriti nell'array
  sbi(PORTD, DDD5);
}

byte byteToLeds(byte data) {
  byte val = 0;
  byte max = data / 28;
  for (byte i = 0; i < max; i++) {
    val |= 1;
    if (i != max - 1) {
      val <<= 1;
    }
  }
  return val;
}

void ledShutdown(bool shutdown) {
  spiSend(OP_SHUTDOWN, shutdown ? 0 : 1);
}

inline byte flipByte(byte c){
  char r = 0;
  
  for(byte i = 0; i < 8; i++){
    r <<= 1;
    r |= c & 1;
    c >>= 1;
  }

  return r;
}

void ledSet(int row, int col, bool state) {
  // 0 <= row <= 7
  // 0 <= col <= 7
  
  byte val = 1 << (7 - col);
  
  if(state) {
    spistatus[row] |= val;
  } else {
    spistatus[row] &= ~val;
  }

  spiSend(row + 1, spistatus[row]);
}

void ledSetRow(int row, byte value) {
  for(int col = 0; col < 8; col++) {
    byte val = value >> col;
    val &= 0x01;
    ledSet(col, row, val);
  }
}

void ledSetCol(int col, byte value) {
  // 0 <= col <= 7

  spistatus[col] = flipByte(value);;
  spiSend(col + 1, spistatus[col]);
}

void ledClear() {
  for(int i = 0; i < 8; i++) {
    spistatus[i] = 0;
    spiSend(i + 1, spistatus[i]);
  }
}

void ledInit() {
  /*
    MOSI  = 6
    CS    = 5
    CLK   = 4
  */
  
  // Imposto i pin MOSI, CS e CLK come OUTPUT
  sbi(DDRD, DDD6);
  sbi(DDRD, DDD5);
  sbi(DDRD, DDD4);

  // Attivo il pin CHIP SELECT, impostandolo a HIGH
  sbi(PORTD, DDD5);
  
  // Inizializzo la matrice
  spiSend(OP_DISPLAYTEST, 0);
  spiSend(OP_SCANLIMIT,   7);
  spiSend(OP_DECODEMODE,  0);

  ledClear();

  // E' necessario spegnere e riaccendere la matrice LED
  ledShutdown(true);
  ledShutdown(false);
}

/* Fine gestione matrice LED */


/* Inizio primitive */

int readAnalog(int analogPin) {
  // Se il pin è >= 14 sottraggo 14, in questo modo è possibile utilizzare le notazioni Ax per indicare il pin (ex. A1 = 15, A5 = 19)
	analogPin -= analogPin >= 14 ? 14 : 0;

  // Inizializzo la conversione A -> D
  ADMUX = DEFAULT << 6;

  // Imposto il pin analogico
  ADMUX |= analogPin;

  // Abilito la conversione A -> D
  sbi(ADCSRA, ADSC);
  
  // Aspetto che termini la conversione
  // Per capire quando è terminata la conversione faccio fede al bit ADSC del registro ADCSRA (1 = in corso, 0 = terminata)
  while (ADCSRA & _BV(ADSC))
    ;

  // ADCL va preso per primo
	return ADCL | (ADCH << 8);
}

byte readAnalogByte(int analogPin) {
  // Ignoro i due bit meno significativi per ritornare un byte (la dimensione del valore letto dall'ADC è 10bit)
  return readAnalog(analogPin) >> 2;
}

/* Fine primitive */


// Inizio registrazione
void startRecording() {
  // Imposto la variabile di stato
  isRecording = true;

  // Imposto il numero di letture prima di stampare a video
  maxReadings = timeBase / 32 * READ_TIME;
  currentReadings = 0;

  // Pulisco la matrice LED
  ledClear();

  // Imposto la colonna di partenza
  currentCol = 0;

  // Accendo il led
  sbi(PORTD, DDD2);

  // Disabilito gli interrupt utilizzando la funzione da me definita (non tanto diversa da quella di default)
  cli();

  // Cancello ADLAR (5) in ADMUX per allineare a destra il risultato
  // Gli 8 bit inferiori andranno in ADCL, mentre i 2 superiori andranno in ADCH
  cbi(ADMUX, ADLAR);
 
  // Metto REFS0 (6) a 1 in ADMUX per impostare la sorgente giusta (01)
  sbi(ADMUX, REFS0);
 
  // Cancello da MUX3 a MUX0 in ADMUX in preparazione all'assegnazioe del pin analogico
  cbi(ADMUX, MUX3);
  cbi(ADMUX, MUX2);
  cbi(ADMUX, MUX1);
  cbi(ADMUX, MUX0);
 
  // Imposto il pin analogico da controllare (il 5 in questo caso)
  // L'AND serve per essere sicuri di non superare il limite di 4 bit (0xF = 15 = 0b1111)
  ADMUX |= 4 & 0xF;
 
  // Imposto ADEN (7) in ADCSRA per abilitare l'ADC.
  // N.B. per eseguire questa istruzione servono 12 cicli di clock ADC
  sbi(ADCSRA, ADEN);

  // Imposto ADATE (5) in ADCSRA per abilitare l'auto-triggering.
  sbi(ADCSRA, ADATE);

  // Cancello da ADTS2 a ADTS0 in ADCSRB per impostare la modalità di trigger a "free running".
  // Significa che appena una conversione ADC termina, ne verrà fatta una nuova
  cbi(ADCSRB, ADTS2);
  cbi(ADCSRB, ADTS1);
  cbi(ADCSRB, ADTS0);
 
  // Imposto il prescaler a 128 (16000KHz/128 = 125KHz)
  // Sopra i 200KHz i risultati non sono affidabili.
  // Tempo di conversione: 1/125000 * 13
  sbi(ADCSRA, CS10);
  sbi(ADCSRA, CS11);
  sbi(ADCSRA, CS12);
 
  // Imposto ADIE in ADCSRA per abilitare l'interrupt ADC.
  sbi(ADCSRA, ADIE);

  // Abilito gli interrupt utilizzando la funzione da me definita
  sei();

  // Imposto ADSC in ADCSRA per iniziare la conversione ADC (la prima volta è necessario, poi continua automaticamente fino all'impostazione di ADSC a 0)
  sbi(ADCSRA, ADSC);
}

// Fine registrazione
void stopRecording() {
  // Spengo il led
  cbi(PORTD, DDD2);

  // Cancello ADSC (6) da ADCSRA per fermare la conversione analogico-digitale
  cbi(ADCSRA, ADSC);
  // Cancello ADATE (5) da ADCSRA per disabilitare l'auto-triggering.
  cbi(ADCSRA, ADATE);
  // Cancello ADIE (3) da ADCSRA per disabilitare l'interrupt ADC.
  cbi(ADCSRA, ADIE);

  // Pulisco la matrice led
  ledClear();

  // Imposto la variabile di stato
  isRecording = false;
}

// Interrupt service routine per il completamnto della conversione ADC
ISR(ADC_vect){
  // Cicli di clock: 67
  
  // Bisogna leggere ADCL per primo
  readData = (readData + ((ADCL | (ADCH << 8)) >> 2)) / 2;
  currentReadings++;
}

/*

Disassembly della funzione qui sopra

00000530 <__vector_21>:
 530:   1f 92           push    r1
 532:   0f 92           push    r0
 534:   0f b6           in      r0, 0x3f        ; 63
 536:   0f 92           push    r0
 538:   11 24           eor     r1, r1
 53a:   2f 93           push    r18
 53c:   3f 93           push    r19
 53e:   8f 93           push    r24
 540:   9f 93           push    r25
 542:   20 91 78 00     lds     r18, 0x0078     ; 0x800078 <__DATA_REGION_ORIGIN__+0x18>
 546:   80 91 79 00     lds     r24, 0x0079     ; 0x800079 <__DATA_REGION_ORIGIN__+0x19>
 54a:   38 2f           mov     r19, r24
 54c:   35 95           asr     r19
 54e:   27 95           ror     r18
 550:   35 95           asr     r19
 552:   27 95           ror     r18
 554:   80 91 f0 01     lds     r24, 0x01F0     ; 0x8001f0 <readData>
 558:   82 0f           add     r24, r18
 55a:   93 2f           mov     r25, r19
 55c:   91 1d           adc     r25, r1
 55e:   97 fd           sbrc    r25, 7
 560:   01 96           adiw    r24, 0x01       ; 1
 562:   95 95           asr     r25
 564:   87 95           ror     r24
 566:   80 93 f0 01     sts     0x01F0, r24     ; 0x8001f0 <readData>
 56a:   80 91 f4 01     lds     r24, 0x01F4     ; 0x8001f4 <currentReadings>
 56e:   90 91 f5 01     lds     r25, 0x01F5     ; 0x8001f5 <currentReadings+0x1>
 572:   01 96           adiw    r24, 0x01       ; 1
 574:   90 93 f5 01     sts     0x01F5, r25     ; 0x8001f5 <currentReadings+0x1>
 578:   80 93 f4 01     sts     0x01F4, r24     ; 0x8001f4 <currentReadings>
 57c:   9f 91           pop     r25
 57e:   8f 91           pop     r24
 580:   3f 91           pop     r19
 582:   2f 91           pop     r18
 584:   0f 90           pop     r0
 586:   0f be           out     0x3f, r0        ; 63
 588:   0f 90           pop     r0
 58a:   1f 90           pop     r1
 58c:   18 95           reti

Somma dei cicli di clock delle istruzioni soprastanti: 66~68, dipendentemente dallo stato dei registri.
Facendo una media assumiamo 67 cicli
*/