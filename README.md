****Thread Local Storage****
  
This project was implemented by Amruth Niranjan at Boston University.

Sources used: Man pages, slides

- tls_create(): Checked through all of hash map to see if thread with pthread_self() returned TID already has a local storage area, in which case throw an error. Same for size <=0. Then, I allocated space for a TLS structure that I had specified globally using calloc, and then configured each member of this structure. I called it threadlocs and initialized its tid, size, and page_num, which was calculated by dividing size by page_size and adding one if there was any remainder. Then I allocated space for threadlocs->pages using calloc, and then allocated all the pages for threadlocs using the provided structure in the slides. Lastly I added it back into the hash map by applying modulus to its tid.

- tls_destroy():  Checked through all of hash map to see if thread with TID tid had a local storage area, if not, threw an error. I then remove it from the hash map. I cleaned the pages by decrementing reference count if it was > 1, otherwise freeing the page and unmapping it from memory. Then I freed all of the calloc'ed and malloc'ed data being used by the hash_element like the pages array.

- tls_clone(): Make sure target thread has a local storage area (iter) to work with, and make sure thread to-be-cloned (new_item) DOESN'T hasve a local storage area. Then allocate space for this new element using calloc, and allocate space for its local storage. Initialize all members and then copy pages page by page, then add to global hash map.

- tls_write(): Check if thread has an LSA by looking thru hash map using modulus, unprotect pages for r/w access, perform writeop (in slides), and reprotect pages

- tls_read(): Check if thread has an LSA by looking thru hash map using modulus, unprotect pages for r/w access, perform readop (in slides), and reprotect pages

Used a simple hash map of fixed size 128 (specified in slides) for global data structure implementation. tls_init() and tls_handle_page_fault() implementations as well as other global variables and data structures are from the slides.


Please let me know if there are any bugs! Thank you and enjoy!

Amruth Niranjan
