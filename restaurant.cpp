#include "restaurant.h"

/*
	Sets *ptr to the i'th restaurant. If this restaurant is already in the cache,
	it just copies it directly from the cache to *ptr. Otherwise, it fetches
	the block containing the i'th restaurant and stores it in the cache before
	setting *ptr to it.
*/
void getRestaurant(restaurant* ptr, int i, Sd2Card* card, RestCache* cache) {
	// calculate the block with the i'th restaurant
	uint32_t block = REST_START_BLOCK + i/8;

	// if this is not the cached block, read the block from the card
	if (block != cache->cachedBlock) {
		while (!card->readBlock(block, (uint8_t*) cache->block)) {
			Serial.print("readblock failed, try again");
		}
		cache->cachedBlock = block;
	}

	// either way, we have the correct block so just get the restaurant
	*ptr = cache->block[i%8];
}

// Swaps the two restaurants (which is why they are pass by reference).
void swap(RestDist& r1, RestDist& r2) {
	RestDist tmp = r1;
	r1 = r2;
	r2 = tmp;
}

// Insertion sort to sort the restaurants.
void insertionSort(RestDist restaurants[], int n) {
	// n is number to sort
	// Invariant: at the start of iteration i, the
	// array restaurants[0 .. i-1] is sorted.
	for (int i = 1; i < n; ++i) {
		// Swap restaurant[i] back through the sorted list restaurants[0 .. i-1]
		// until it finds its place.
		for (int j = i; j > 0 && restaurants[j].dist < restaurants[j-1].dist; --j) {
			swap(restaurants[j-1], restaurants[j]);
		}
	}
}

// pivot function used in quickSort
int pivot(RestDist restaurants[], int start, int end) {
	int pi = restaurants[end].dist;
	int i = (start - 1);

	for (int j = start; j <= end - 1; j++) {
		if (restaurants[j].dist <= pi) {
			i++;
			swap(restaurants[i], restaurants[j]);
		}
	}

	swap(restaurants[i+1], restaurants[end]);
	return i+1;
}

// quickSort function
void quickSort(RestDist restaurants[], int start, int end) {
	if (start < end) {
		int pi = pivot(restaurants, start, end);
		quickSort(restaurants, start, pi - 1);
		quickSort(restaurants, pi + 1, end);
	}
}

// Computes the manhattan distance between two points (x1, y1) and (x2, y2).
int16_t manhattan(int16_t x1, int16_t y1, int16_t x2, int16_t y2) {
	return abs(x1-x2) + abs(y1-y2);
}

int generateList(int rateSelect, const MapView& mv, RestDist restaurants[], Sd2Card* card, RestCache* cache) {
	restaurant r;
	int j = 0;
	for (int i = 0; i < NUM_RESTAURANTS; i++) {
		getRestaurant(&r, i, card, cache);
		int newRating = max((r.rating + 1)/2, 1);
		if (newRating >= rateSelect) {
			restaurants[j].index = i;
			restaurants[j].dist = manhattan(lat_to_y(r.lat), lon_to_x(r.lon),
								mv.mapY + mv.cursorY, mv.mapX + mv.cursorX);
			j++;
		}
	}

	return j;
}

/*
	Fetches all restaurants from the card, saves their RestDist information
	in restaurants[], and then sorts them based on their distance to the
	point on the map represented by the MapView.
*/
int getAndSortRestaurants(const MapView& mv, RestDist restaurants[], Sd2Card* card, RestCache* cache, int rateSelect, int sortSelect) {
	int relevant;
	// First get all the restaurants and store their corresponding RestDist information.
	if (sortSelect == 0 || sortSelect == 1) {
		relevant = generateList(rateSelect, mv, restaurants, card, cache);
		if (sortSelect == 0) {
			uint32_t time1 = millis();
			quickSort(restaurants, 0, relevant);
			uint32_t time2 = millis();
			Serial.print("Qsort Time: ");
			Serial.println(time2 - time1);
		} else {
			uint32_t time1 = millis();
			insertionSort(restaurants, relevant);
			uint32_t time2 = millis();
			Serial.print("Isort Time: ");
			Serial.println(time2 - time1);
		}
	} else if (sortSelect == 2) {
		// both
		relevant = generateList(rateSelect, mv, restaurants, card, cache);
		uint32_t time1 = millis();
		insertionSort(restaurants, relevant);
		uint32_t time2 = millis();
		Serial.print("Isort Time: ");
		Serial.println(time2 - time1);
		relevant = generateList(rateSelect, mv, restaurants, card, cache);
		time1 = millis();
		quickSort(restaurants, 0, relevant);
		time2 = millis();
		Serial.print("Qsort Time: ");
		Serial.println(time2 - time1);
	}

	return relevant;
}
