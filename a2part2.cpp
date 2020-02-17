//------------------------------
// Name: Celine Fong (1580124)
//		 Claire Martin ()
// CMPUT 275, Winter 2020
// Major Assignment #1, Part #2
//------------------------------
	
#include <Arduino.h>
#include <MCUFRIEND_kbv.h>
#include <SPI.h>
#include <SD.h>
#include <TouchScreen.h>
#include <Adafruit_GFX.h>
#include "lcd_image.h"
#include "yegmap.h"
#include "restaurant.h"

// SD_CS pin for SD card reader
#define SD_CS 10

// joystick pins
#define JOY_VERT_ANALOG  A9
#define JOY_HORIZ_ANALOG A8
#define JOY_SEL 53

// width/height of the display when rotated horizontally
#define TFT_WIDTH 480
#define TFT_HEIGHT 320

// layout of display for this assignment,
#define RATING_SIZE 60
#define DISP_WIDTH (TFT_WIDTH - RATING_SIZE)
#define DISP_HEIGHT TFT_HEIGHT

// constants for the joystick
#define JOY_DEADZONE 64
#define JOY_CENTRE 512
#define JOY_STEPS_PER_PIXEL 64

// touch screen pins, obtained from the documentaion
#define YP A3  // must be an analog pin, use "An" notation!
#define XM A2  // must be an analog pin, use "An" notation!
#define YM  9  // can be a digital pin
#define XP  8  // can be a digital pin

// calibration data for the touch screen, obtained from documentation
// the minimum/maximum possible readings from the touch point
#define TS_MINX 100
#define TS_MINY 120
#define TS_MAXX 920
#define TS_MAXY 940

// thresholds to determine if there was a touch
#define MINPRESSURE  100
#define MAXPRESSURE 1000

// Cursor size. For best results, use an odd number.
#define CURSOR_SIZE 9

// number of restaurants to display
#define REST_DISP_NUM 21

// ********** BEGIN GLOBAL VARIABLES ************
MCUFRIEND_kbv tft;
Sd2Card card;

// A multimeter reading says there are 300 ohms of resistance across the plate,
// so initialize with this to get more accurate readings.
TouchScreen ts = TouchScreen(XP, YP, XM, YM, 300);

// The currently selected restaurant, if we are in mode 1.
// It's arguable if this needs to be a global variable, but we'll let this
// be one of the "few" that are allowed. As long as we don't clutter the global
// space with too many variables.
int selectedRest;

// overall restaurant index in sorted list
int overallIndex;

// rating and sort selector variables
int rating = 1;
int sortMode = 0;

// sets number of restaurants to pull from list
int relevantRestaurants = NUM_RESTAURANTS;

// which mode are we in?
enum DisplayMode { MAP, MENU } displayMode;

// the current map view and the previous one from last cursor movement
MapView curView, preView;

// For sorting and displaying the restaurants, will hold the restaurant RestDist
// information for the most recent click in sorted order.
RestDist restaurants[NUM_RESTAURANTS];

// edmonton map
lcd_image_t edmontonBig = { "yeg-big.lcd", MAPWIDTH, MAPHEIGHT };

// The cache of 8 restaurants for the getRestaurant function.
RestCache cache;

// ************ END GLOBAL VARIABLES ***************

// Forward declaration of functions to begin the modes. Setup uses one, so
// it seems natural to forward declare both (not really that important).
void beginMode0();
void beginMode1();
void buttons();

void setup() {
	init();

	Serial.begin(9600);

	// joystick button initialization
	pinMode(JOY_SEL, INPUT_PULLUP);

	// tft display initialization
	uint16_t ID = tft.readID();
	tft.begin(ID);
	tft.setRotation(1);
	tft.setTextWrap(false);

	// now initialize the SD card in both modes
    Serial.print("Initializing SD card...");

	// Initialize for reading through the FAT filesystem
	// (required for lcd_image drawing function).
    if (!SD.begin(SD_CS)) {
    	Serial.println("failed!");
    	Serial.println("Is the card inserted properly?");
    	while (true) {}
    }

	// Also initialize the SD card for raw reads.
    Serial.print("Initializing SPI communication for raw reads...");
    if (!card.init(SPI_HALF_SPEED, SD_CS)) {
    	Serial.println("failed!");
    	while (true) {}
    }

    Serial.println("OK!");

    // initial cursor position is the centre of the screen
    curView.cursorX = DISP_WIDTH/2;
    curView.cursorY = DISP_HEIGHT/2;

	  // initial map position is the middle of Edmonton
	  curView.mapX = ((MAPWIDTH / DISP_WIDTH)/2) * DISP_WIDTH;
	  curView.mapY = ((MAPHEIGHT / DISP_HEIGHT)/2) * DISP_HEIGHT;

		// This ensures the first getRestaurant() will load the block as all blocks
		// will start at REST_START_BLOCK, which is 4000000.
		cache.cachedBlock = 0;

		// will draw the initial map screen and other stuff on the display
	  beginMode0();
}

/* 
	Draw the map patch of edmonton over the preView position, then
	draw the red cursor at the curView position. Taken from provided part 1 solution

	Arguments:
		None

	Returns:
		None
*/
void moveCursor() {
	lcd_image_draw(&edmontonBig, &tft,
								 preView.mapX + preView.cursorX - CURSOR_SIZE/2,
							 	 preView.mapY + preView.cursorY - CURSOR_SIZE/2,
							   preView.cursorX - CURSOR_SIZE/2, preView.cursorY - CURSOR_SIZE/2,
								 CURSOR_SIZE, CURSOR_SIZE);

	tft.fillRect(curView.cursorX - CURSOR_SIZE/2, curView.cursorY - CURSOR_SIZE/2,
							 CURSOR_SIZE, CURSOR_SIZE, TFT_RED);
}

/*
	Set the mode to 0 and draw the map and cursor according to curView. Taken from given part 1 solution.

	Arguments:
		None
	Returns: 
		None
*/
void beginMode0() {
	// Black out the rating selector part (less relevant in Assignment 1, but
	// it is useful when you first start the program).
	tft.fillRect(DISP_WIDTH, 0, RATING_SIZE, DISP_HEIGHT, TFT_BLACK);

	// Draw the current part of Edmonton to the tft display.
  lcd_image_draw(&edmontonBig, &tft,
								 curView.mapX, curView.mapY,
								 0, 0,
								 DISP_WIDTH, DISP_HEIGHT);

  buttons();

	// just the initial draw of the cursor on the map
	moveCursor();

  displayMode = MAP;
}

/* 
	Print the i'th restaurant in the sorted list. Modified from existing part 1 solution to 
	account for i > 21. 

	Arguments:
		i (int): index of restaurant in sorted list

	Returns:
		None
*/
void printRestaurant(int i) {
	restaurant r;

	// get the i'th restaurant
	getRestaurant(&r, restaurants[i].index, &card, &cache);

	// Set its colour based on whether or not it is the selected restaurant.
	if ((i % REST_DISP_NUM) != selectedRest) {
		tft.setTextColor(TFT_WHITE, TFT_BLACK);
	}
	else {
		tft.setTextColor(TFT_BLACK, TFT_WHITE);
	}
	tft.setCursor(0, (i%REST_DISP_NUM)*15);
	tft.print(r.name);
}

/*
	Begin mode 1 by sorting the restaurants around the cursor
	and then displaying the list. Modified from existing part 1 solution.

	Arguments:
		None

	Returns:
		None
*/
void beginMode1() {
	tft.setCursor(0, 0);
	tft.fillScreen(TFT_BLACK);
	tft.setTextSize(2);

	// Get the RestDist information for this cursor position and sort it.
	relevantRestaurants = getAndSortRestaurants(curView, restaurants, &card, &cache, rating, sortMode);

	// Initially have the closest restaurant highlighted.
	selectedRest = 0;

	// initially overall restaurant index should be the same as selectedRest
	overallIndex = 0;

	// Print the list of restaurants.
	for (int i = 0; i < REST_DISP_NUM; ++i) {
		printRestaurant(i);
	}

	displayMode = 1;
}

/*
	Checks if the edge was nudged and scrolls the map if it was. Taken from part 1 solution.

	Arguments:
		None

	Returns: 
		None
*/
void checkRedrawMap() {
  // A flag to indicate if we scrolled.
	bool scroll = false;

	// If we nudged the left or right edge, shift the map over.
	if (curView.cursorX == DISP_WIDTH-CURSOR_SIZE/2-1 && curView.mapX != MAPWIDTH - DISP_WIDTH) {
		curView.mapX += DISP_WIDTH;
		curView.cursorX = DISP_WIDTH/2;
		scroll = true;
	}
	else if (curView.cursorX == CURSOR_SIZE/2 && curView.mapX != 0) {
		 curView.mapX -= DISP_WIDTH;
		 curView.cursorX = DISP_WIDTH/2;
		 scroll = true;
	}

	// If we nudged the top or bottom edge, shift the map up or down.
	if (curView.cursorY == DISP_HEIGHT-CURSOR_SIZE/2-1 && curView.mapY != MAPHEIGHT - DISP_HEIGHT) {
		curView.mapY += DISP_HEIGHT;
		curView.cursorY = DISP_HEIGHT/2;
		scroll = true;
	}
	else if (curView.cursorY == CURSOR_SIZE/2 && curView.mapY != 0) {
		curView.mapY -= DISP_HEIGHT;
		curView.cursorY = DISP_HEIGHT/2;
		scroll = true;
	}

	// If we nudged the edge, recalculate and draw the new rectangular portion of Edmonton to display.
	if (scroll) {
		// Make sure we didn't scroll outside of the map.
		curView.mapX = constrain(curView.mapX, 0, MAPWIDTH - DISP_WIDTH);
		curView.mapY = constrain(curView.mapY, 0, MAPHEIGHT - DISP_HEIGHT);

		lcd_image_draw(&edmontonBig, &tft, curView.mapX, curView.mapY, 0, 0, DISP_WIDTH, DISP_HEIGHT);
	}
}

/*
	Process joystick and touchscreen input when in mode 0. Taken from part1 solution.

	Arguments:
		None

	Returns:
		None
*/
void scrollingMap() {
  int v = analogRead(JOY_VERT_ANALOG);
  int h = analogRead(JOY_HORIZ_ANALOG);
  int invSelect = digitalRead(JOY_SEL);

	// A flag to indicate if the cursor moved or not.
	bool cursorMove = false;

  // If there was vertical movement, then move the cursor.
  if (abs(v - JOY_CENTRE) > JOY_DEADZONE) {
    // First move the cursor.
    int delta = (v - JOY_CENTRE) / JOY_STEPS_PER_PIXEL;
		// Clamp it so it doesn't go outside of the screen.
    curView.cursorY = constrain(curView.cursorY + delta, CURSOR_SIZE/2, DISP_HEIGHT-CURSOR_SIZE/2-1);
		// And now see if it actually moved.
		cursorMove |= (curView.cursorY != preView.cursorY);
  }

	// If there was horizontal movement, then move the cursor.
  if (abs(h - JOY_CENTRE) > JOY_DEADZONE) {
    // Ideas are the same as the previous if statement.
    int delta = -(h - JOY_CENTRE) / JOY_STEPS_PER_PIXEL;
    curView.cursorX = constrain(curView.cursorX + delta, CURSOR_SIZE/2, DISP_WIDTH-CURSOR_SIZE/2-1);
		cursorMove |= (curView.cursorX != preView.cursorX);
  }

	// If the cursor actually moved.
	if (cursorMove) {
		// Check if the map edge was nudged, and move it if so.
		checkRedrawMap();

		preView.mapX = curView.mapX;
		preView.mapY = curView.mapY;

		// Now draw the cursor's new position.
		moveCursor();
	}

	preView = curView;

	// Did we click the joystick?
  if(invSelect == LOW){
		beginMode1();
    displayMode = MENU;
    Serial.println(displayMode);
    Serial.println("MODE changed.");

		// Just to make sure the restaurant is not selected by accident
		// because the button was pressed too long.
		while (digitalRead(JOY_SEL) == LOW) { delay(10); }
  }

	// Check for touchscreen press and draws dots for each restaurant
	TSPoint touch = ts.getPoint();

	// Necessary to resume TFT display functions
	pinMode(YP, OUTPUT);
  	pinMode(XM, OUTPUT);

	// If there was an actual touch, draw the dots
	if (touch.z >= MINPRESSURE && touch.z <= MAXPRESSURE) {
		// if the screen has been touched
	    // map touch points to screen size
	    int ptx = map(touch.y, TS_MINX, TS_MAXX, 0, TFT_WIDTH);
        int pty = map(touch.x, TS_MINY, TS_MAXY, 0, TFT_HEIGHT);
        if (ptx > RATING_SIZE) {
        	// touch was in map range
        	restaurant r;
			// just iterate through all relevant restaurants (preferred rating) on the card
			for (int i = 0; i < relevantRestaurants; ++i) {
				getRestaurant(&r, i, &card, &cache);
				int16_t rest_x_tft = lon_to_x(r.lon)-curView.mapX, rest_y_tft = lat_to_y(r.lat)-curView.mapY;

				// only draw if entire radius-3 circle will be in the map display
				if (rest_x_tft >= 3 && rest_x_tft < DISP_WIDTH-3 &&  rest_y_tft >= 3 && rest_y_tft < DISP_HEIGHT-3) {
					tft.fillCircle(rest_x_tft, rest_y_tft, 3, TFT_BLUE);
				}
			}
        } else if (ptx < RATING_SIZE && pty > (DISP_HEIGHT/2)) {
        	// touch was on buttons
        	rating++;
        	rating = rating % 6;
        	if (rating == 0) {
        		// account for 6 % 6 = 0
        		rating = 1;
        	}
        	buttons();
        	delay(200);
        } else if (ptx < RATING_SIZE && pty < (DISP_HEIGHT/2)) {
        	sortMode ++;
        	sortMode = sortMode % 3;
        	buttons();
        	delay(200);
        }
		
	}
}

/*
	Process joystick movement when in mode 1. Modified from part 1 solution to include overallIndex.
	
	Arguments:
		None

	Returns:
		None
*/
void scrollingMenu() {
	int oldRest = selectedRest;
	int overallIndexPrev = overallIndex;

	int v = analogRead(JOY_VERT_ANALOG);

	// if the joystick was pushed up or down, change restaurants accordingly.
	if (v > JOY_CENTRE + JOY_DEADZONE) {
		// increment overall index along with selectedRest so they always point to the same restaurant
		++selectedRest;
		++overallIndex;
	}
	else if (v < JOY_CENTRE - JOY_DEADZONE) {
		// decrement overall index along with selectedRest so they always point to the same restaurant
		--selectedRest;
		--overallIndex;
	}

	// if the selected restaurant has exceeded number of displayed restaurants on screen
	if (selectedRest > REST_DISP_NUM - 1 && overallIndex < relevantRestaurants) {
		// reset the screen
		tft.fillScreen(TFT_BLACK);
		// reset the selected rest to 0
		selectedRest = 0;
		// draw the next 21 restaurants on a new page
		for (int i = 0; i < REST_DISP_NUM; ++i) {
			printRestaurant(i + overallIndex);
		}
	} else if (selectedRest < 0 && overallIndex >= 0) {
		// reset the screen
		tft.fillScreen(TFT_BLACK);
		// reset the selected rest to 21
		selectedRest = 20;
		// draw previous 21 restaurants on new page
		for (int i = 0; i < REST_DISP_NUM; ++i) {
			printRestaurant(overallIndex - i);
		}
	} else {
		// constrain the overallIndex variable to number of restaurants
		overallIndex = constrain(overallIndex, 0, relevantRestaurants);

		// constrain the selected restaurant varaible to number of displayable restaurants
		selectedRest = constrain(selectedRest, 0, REST_DISP_NUM);

		// If we picked a new restaurant, update the way it and the previously
		// selected restaurant are displayed.
		if (oldRest != selectedRest) {
			printRestaurant(overallIndexPrev);
			printRestaurant(overallIndex);
			delay(50); // so we don't scroll too fast
		}		
	}

	// If we clicked on a restaurant.
	if (digitalRead(JOY_SEL) == LOW) {
		restaurant r;
		getRestaurant(&r, restaurants[overallIndex].index, &card, &cache);
		// Calculate the new map view.

		// Center the map view at the restaurant, constraining against the edge of
		// the map if necessary.
		curView.mapX = constrain(lon_to_x(r.lon)-DISP_WIDTH/2, 0, MAPWIDTH-DISP_WIDTH);
		curView.mapY = constrain(lat_to_y(r.lat)-DISP_HEIGHT/2, 0, MAPHEIGHT-DISP_HEIGHT);

		// Draw the cursor, clamping to an edge of the map if needed.
		curView.cursorX = constrain(lon_to_x(r.lon) - curView.mapX, CURSOR_SIZE/2, DISP_WIDTH-CURSOR_SIZE/2-1);
		curView.cursorY = constrain(lat_to_y(r.lat) - curView.mapY, CURSOR_SIZE/2, DISP_HEIGHT-CURSOR_SIZE/2-1);

		preView = curView;

		beginMode0();

		// Ensures a long click of the joystick will not register twice.
		while (digitalRead(JOY_SEL) == LOW) { delay(10); }
	}
}

/*
	Draws buttons on right side of screen which control rating and sort type. 

	Arguments:
		None

	Returns:
		None
*/
void buttons() {
	// draw top button
	tft.drawRect(DISP_WIDTH, 0, RATING_SIZE, DISP_HEIGHT/2, TFT_WHITE);
	// draw bottom button
	tft.drawRect(DISP_WIDTH, DISP_HEIGHT/2, RATING_SIZE, DISP_HEIGHT/2, TFT_WHITE);

	// label top button according to rating
	if (rating == 1) {
		tft.drawChar(DISP_WIDTH + (RATING_SIZE/2)-5, DISP_HEIGHT/4 - 8, '1', TFT_WHITE, TFT_BLACK, 2);
	} else if (rating == 2) {
		tft.drawChar(DISP_WIDTH + (RATING_SIZE/2)-5, DISP_HEIGHT/4 - 8, '2', TFT_WHITE, TFT_BLACK, 2);
	} else if (rating == 3) {
		tft.drawChar(DISP_WIDTH + (RATING_SIZE/2)-5, DISP_HEIGHT/4 - 8, '3', TFT_WHITE, TFT_BLACK, 2);
	} else if (rating == 4) {
		tft.drawChar(DISP_WIDTH + (RATING_SIZE/2)-5, DISP_HEIGHT/4 - 8, '4', TFT_WHITE, TFT_BLACK, 2);
	} else {
		tft.drawChar(DISP_WIDTH + (RATING_SIZE/2)-5, DISP_HEIGHT/4 - 8, '5', TFT_WHITE, TFT_BLACK, 2);
	}

	// label bottom button according to sort mode
	if (sortMode == 1) {
		tft.drawChar(DISP_WIDTH + (RATING_SIZE/2) - 5, 3*(DISP_HEIGHT/4) - 40, 'I', TFT_WHITE, TFT_BLACK, 2);
		tft.drawChar(DISP_WIDTH + (RATING_SIZE/2) - 5, 3*(DISP_HEIGHT/4) - 24, 'S', TFT_WHITE, TFT_BLACK, 2);
		tft.drawChar(DISP_WIDTH + (RATING_SIZE/2) - 5, 3*(DISP_HEIGHT/4) - 8, 'O', TFT_WHITE, TFT_BLACK, 2);
		tft.drawChar(DISP_WIDTH + (RATING_SIZE/2) - 5, 3*(DISP_HEIGHT/4) + 8, 'R', TFT_WHITE, TFT_BLACK, 2);
		tft.drawChar(DISP_WIDTH + (RATING_SIZE/2) - 5, 3*(DISP_HEIGHT/4) + 24, 'T', TFT_WHITE, TFT_BLACK, 2);
	} else if (sortMode == 0) {
		tft.drawChar(DISP_WIDTH + (RATING_SIZE/2) - 5, 3*(DISP_HEIGHT/4) - 40, 'Q', TFT_WHITE, TFT_BLACK, 2);
		tft.drawChar(DISP_WIDTH + (RATING_SIZE/2) - 5, 3*(DISP_HEIGHT/4) - 24, 'S', TFT_WHITE, TFT_BLACK, 2);
		tft.drawChar(DISP_WIDTH + (RATING_SIZE/2) - 5, 3*(DISP_HEIGHT/4) - 8, 'O', TFT_WHITE, TFT_BLACK, 2);
		tft.drawChar(DISP_WIDTH + (RATING_SIZE/2) - 5, 3*(DISP_HEIGHT/4) + 8, 'R', TFT_WHITE, TFT_BLACK, 2);
		tft.drawChar(DISP_WIDTH + (RATING_SIZE/2) - 5, 3*(DISP_HEIGHT/4) + 24, 'T', TFT_WHITE, TFT_BLACK, 2);
	} else {
		tft.fillRect(DISP_WIDTH + (RATING_SIZE/2) - 5, 3*(DISP_HEIGHT/4) - 40, 10, 80, TFT_BLACK);
		tft.drawChar(DISP_WIDTH + (RATING_SIZE/2) - 5, 3*(DISP_HEIGHT/4) - 32, 'B', TFT_WHITE, TFT_BLACK, 2);
		tft.drawChar(DISP_WIDTH + (RATING_SIZE/2) - 5, 3*(DISP_HEIGHT/4) - 16, 'O', TFT_WHITE, TFT_BLACK, 2);
		tft.drawChar(DISP_WIDTH + (RATING_SIZE/2) - 5, 3*(DISP_HEIGHT/4), 'T', TFT_WHITE, TFT_BLACK, 2);
		tft.drawChar(DISP_WIDTH + (RATING_SIZE/2) - 5, 3*(DISP_HEIGHT/4) + 16, 'H', TFT_WHITE, TFT_BLACK, 2);
	}
}

int main() {
	setup();

	// All the implementation work is done now, just have a loop that processes
	// the appropriate function!
	while (true) {
		if (displayMode == MAP) {
			scrollingMap();
		}
		else {
			scrollingMenu();
		}
	}

	Serial.end();
	return 0;
}
