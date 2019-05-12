# netstore

Zadanie polega na napisania serwera i klienta do pobierania fragmentów plików z innego komputera.
Zakładamy, że dostępne pliki są krótsze niż 4 GiB. Używać będziemy protokołu TCP i adresowania IPv4.

## Klient

Klient wysyła do serwera polecenia. Są dwa rodzaje poleceń:
* prośba o przesłanie listy dostępnych plików;
* żądanie przysłania fragmentu pliku o podanej nazwie.

Rodzaj polecenia podany jest w jego dwóch początkowych bajtach jako 2-bajtowa liczba całkowita bez znaku:
* `1` oznacza prośbę o przysłanie listy plików,
* `2` oznacza żądanie przysłania fragment pliku.

Prośba o przysłanie listy plików nie zawiera nic więcej.

Żądanie przysłania fragmentu pliku zawiera kolejne pola:
* adres początku fragmentu w bajtach, 4-bajtowa liczba całkowita bez znaku;
* liczba bajtów do przesłania, typ jw.;
* długość nazwy pliku w bajtach, 2-bajtowa liczba całkowita bez znaku;
* nazwa pliku niezakończona bajtem zerowym.

Program klienta uruchamia się następująco:
```
netstore-client <nazwa-lub-adres-IP4-serwera> [<numer-portu-serwera>]
```

Domyślny numer portu to `6543`.

Klient łączy się z serwerem po TCP, wysyła prośbę o listę plików i oczekuje odpowiedzi zawierającej listę dostępnych plików.

Po otrzymaniu listy plików klient powinien wyświetlić ją użytkownikowi na standardowe wyjście, każda nazwa w nowym wierszu. Nazwy należy poprzedzić kolejnym numerem i znakiem kropki. Następnie ze standardowego wejścia należy pobrać numer pliku, adres początku fragmentu i adres końca fragmentu, każda wartość w osobnym wierszu.

Otrzymane wartości należy obudować żądaniem przysłania fragmentu pliku o formacie podanym powyżej i wysłać do serwera. Po wysłaniu klient oczekuje na odpowiedź.

Odpowiedź może być:
* odmową: któraś z podanych wartości jest błędna;
* ciągiem danych z zawartością fragmentu, otrzymane dane należy zapisać do pliku o tej samej nazwie co źródło, ale w podkatalogu `tmp` bieżącego katalogu. Dane powinny trafić w to samo miejsce w docelowym pliku co w pliku, z którego były pobrane.

Jeśli plik istnieje, to nie czyścimy go, tylko nadpisujemy wskazane bajty. Jeśli plik nie istnieje, to oczywiście go tworzymy.

Może się zdarzyć, że zapisanie fragmentu w jego miejscu spowoduje powstanie „dziury”. Jest to sytuacja poprawna. W kolejnym pobraniu być może załatamy tę dziurę albo wypełnimy ją w inny sposób.

Po pozytywnym odebraniu wszystkich bajtów fragmentu należy zakończyć pracę.

W przypadku odmowy przyczynę odmowy należy wypisać użytkownikowi, po czym zakończyć pracę.

## Serwer

Program serwera uruchamia się następująco:
```
netstore-server <nazwa-katalogu-z-plikami> [<numer-portu-serwera]
```

Domyślny numer portu to `6543`.

Serwer po wystartowaniu oczekuje na polecenia klientów.

Po nawiązaniu połączenia serwer oczekuje na prośbę o przysłanie listy nazw dostępnych plików.

Odpowiedź z listą dostępnych plików zawiera na początku dwubajtową liczbę całkowitą `1`.

Kolejne pola to:
* długość pola z nazwami plików: 4-bajtowa liczba całkowita bez znaku;
* nazwy plików, rozdzielane znakiem `|` (kreska pionowa).

Zakładamy, że nazwy wszystkich plików są w ASCII i nie zawierają znaków o kodach mniejszych niż 32 ani znaku `|`.

Jeśli zamiast prośby o listę plików serwer od razu otrzyma żadanie fragmentu pliku, to traktuje to jako sytuację poprawną i przechodzi do części opisanej poniżej. Taka sytuacja nie jest możliwa w naszym kliencie (bo użytkownik nie ma tam jak wprowadzić nazwy pliku), ale zgodna z ogólnym protokołem.

Po wysłaniu listy plików serwer oczekuje na żądania klienta.

Dla każdego żądania możliwe są dwie reakcje.

* Wykonanie żądania nie jest możliwe:
  * zła nazwa pliku (być może plik w międzyczasie zniknął),
  * nieprawidłowy (w danym momencie) adres początku fragmentu: większy niż (`rozmiar-pliku - 1`),
  * podano zerowy rozmiar fragmentu.

  Odmowa zaczyna się dwubajtowym polem z liczbą `2`, powód odmowy jest podany w kolejnym polu 4-bajtowym zawierającym podtyp (wartość odpowiednio `1`, `2` lub `3` dla powyższych błędów).

  Po wysłaniu odmowy serwer łagodnie zamyka połączenie.

* Żądanie jest wykonalne. Jeśli rozmiar fragmentu jest za duży, to wysłane będą wszystkie bajty do końca pliku (czyli rozmiar zostanie ,,obcięty'').
  
  Serwer zaczyna wysyłać podany fragment. Na początku wysyła dwubajtowe pole z liczbą `3`, następnie (być może zmodyfikowaną, patrz wyżej) długość fragmentu na 4 bajtach. Potem idą już bajty fragmentu.

Ponieważ pliki (i fragmenty) bywają spore i nie ma sensu trzymanie bufora, który pomieściłby cały wysyłany plik lub jego fragment, to wstawianie danych do wysłania powinno odbywać się porcjami po około 512 KiB.

Po udanym wysłaniu wszystkich bajtów serwer czeka na zamknięcie połączenia, po czym obsługuje następnego klienta.

## Dodatkowe wymagania

Liczby całkowite identyfikujące rodzaj polecenia lub odpowiedzi, adres początku fragmentu czy jego rozmiar, przesyłamy w sieciowej kolejności bajtów.

Katalog z rozwiązaniem powinien zawierać pliki źródłowe `netstore-client.c` i `netstore-server.c` oraz plik `Makefile` zapewniający automatyczną kompilację i linkowanie. Można też umieścić tam inne pliki potrzebne do skompilowania i uruchomienia programu, jeśli to jest konieczne. Ponadto makefile powinien obsługiwać cel `clean`, który po wywołaniu kasuje wszystkie pliki powstałe podczas kompilacji.

Nie wolno używać dodatkowych bibliotek, czyli innych niż standardowa biblioteka C.
