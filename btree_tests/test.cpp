#include <algorithm>
#include <functional>
#include <random>
#include <sys/time.h>
#include <vector>
#include <set>
#include <ParallelTools/parallel.h>

#include <tlx/container/btree_set.hpp>

#define CORRECTNESS 1
#define TRIALS 5
static long get_usecs() {
  struct timeval st;
  gettimeofday(&st, NULL);
  return st.tv_sec * 1000000 + st.tv_usec;
}

template <class T>
std::vector<T> create_random_data(size_t n, size_t max_val,
                                  std::seed_seq &seed) {

  std::mt19937_64 eng(seed); // a source of random data

  std::uniform_int_distribution<T> dist(0, max_val);
  std::vector<T> v(n);

  generate(begin(v), end(v), bind(dist, eng));
  return v;
}

template <class T>
std::tuple<bool, uint64_t, uint64_t, uint64_t, uint64_t>
test_concurrent_btreeset(uint64_t max_size, std::seed_seq &seed) {
  // std::vector<T> data =
  //     create_random_data<T>(max_size, 10000, seed);
  std::vector<T> data =
      create_random_data<T>(max_size, std::numeric_limits<T>::max(), seed);

  for(uint32_t i = 0; i < max_size; i++) {
    data[i]++; // no zeroes
  }
  uint64_t start, end;
#if CORRECTNESS
  std::set<T> serial_set;
  std::vector<T> inserted_elts;
  std::set<T> duplicated_elts;

  start = get_usecs();
  for (uint32_t i = 0; i < max_size; i++) {
    // if (std::count(serial_set.begin(), serial_set.end(), data[i])) {
    //   printf("inserting twice %lu\n", data[i]);
    //   duplicated_elts.insert(data[i]);
    // }
    serial_set.insert(data[i]);
  }
  printf("%lu unique elements\n", serial_set.size());
  end = get_usecs();

  for (auto e: serial_set) {
    inserted_elts.push_back(e);
  }
  // int64_t serial_time = end - start;
  // printf("inserted all the data serially in %lu\n", end - start);
#endif

  tlx::btree_set<T, std::less<T>, tlx::btree_default_traits<T, T>,
                 std::allocator<T>, false>
      serial_test_set;
  start = get_usecs();
  for(uint32_t i = 0; i < max_size; i++) {
    serial_test_set.insert(data[i]);
  }
  end = get_usecs();
  uint64_t serial_time = end - start;
  printf("\tinserted %d elts serially in %lu\n", max_size, end - start);

  tlx::btree_set<T, std::less<T>, tlx::btree_default_traits<T, T>,
                 std::allocator<T>, true>
      concurrent_set;
  start = get_usecs();
  cilk_for(uint32_t i = 0; i < max_size; i++) {
    concurrent_set.insert(data[i]);
  }
  end = get_usecs();
  uint64_t parallel_time = end - start;
  printf("\tinserted %d elts concurrently in %lu\n", max_size, end - start);

#if CORRECTNESS
  bool wrong = false;
  for(auto e : serial_set) {
    // might need to change if you get values because exists takes in a key
    if(!concurrent_set.exists(e)) {
      printf("insertion, didn't find %lu\n", e);
      wrong = true;
    }
  }
  if (wrong) {
    return {false, 0, 0, 0, 0};
  }
#endif

  std::vector<uint64_t> indxs_to_remove =
    create_random_data<uint64_t>(max_size / 2, data.size(), seed);

#if CORRECTNESS
  std::vector<T> elems_removed;
  for (uint32_t i = 0; i < indxs_to_remove.size(); i++) {
    serial_set.erase(data[indxs_to_remove[i]]);
    elems_removed.push_back(data[indxs_to_remove[i]]);
    if (std::count(duplicated_elts.begin(), duplicated_elts.end(), data[indxs_to_remove[i]])) {
      printf("trying to delete a duplicated elt %lu\n", data[indxs_to_remove[i]]);
    }
  }
#endif

  uint64_t serial_remove_start = get_usecs();
  for (uint32_t i = 0; i < indxs_to_remove.size(); i++) {
    serial_test_set.erase(data[indxs_to_remove[i]]);
  }
  uint64_t serial_remove_end = get_usecs();
  uint64_t serial_remove_time = serial_remove_end - serial_remove_start;

  printf("removed half the data serially in %lu\n",
         serial_remove_end - serial_remove_start);
         
  uint64_t parallel_remove_start = get_usecs();
  cilk_for(uint32_t i = 0; i < indxs_to_remove.size(); i++) {
    concurrent_set.erase(data[indxs_to_remove[i]]);
  }

  uint64_t parallel_remove_end = get_usecs();
  uint64_t parallel_remove_time = parallel_remove_end - parallel_remove_start;


  printf("removed half the data in parallel in %lu\n",
         parallel_remove_end - parallel_remove_start);

#if DEBUG

  // for (uint32_t i = 0; i < indxs_to_remove.size(); i++) {
  //   serial_set.erase(data[indxs_to_remove[i]]);
  // }

  // if (serial_set.size() != serial_test_set.size()) {
  //   printf("the sizes of serial set and correct set don't match, got %lu, expected %lu\n",
  //          serial_test_set.size(), serial_set.size());
  //   return {false, 0, 0, 0, 0};
  // }

  // Extensive deletion correctness check after every delete
  // std::vector<uint64_t> elems_removed;
  // for (uint32_t i = 0; i < indxs_to_remove.size(); i++) {
  //   // do a single delete
  //   auto delete_elt = data[indxs_to_remove[i]];
  //   serial_test_set.erase(delete_elt);
  //   serial_set.erase(delete_elt);
  //   elems_removed.push_back(delete_elt);

    // printf("Deleting element, i = %u, elt = %lu\n", i, delete_elt);

    /*
    for(int j = 0; j < inserted_elts.size(); j++) {
      auto check_elt = inserted_elts[j];
      bool was_removed = std::count(elems_removed.begin(), elems_removed.end(), check_elt);

      // do correctness check for checker set first

      // // if removed, check it's removed from set. if not, check it exists
      // if (was_removed) {
      //   // might need to change if you get values because exists takes in a key
      //   if(std::count(serial_set.begin(), serial_set.end(), check_elt)) {
      //     printf("checker set, didn't delete %lu after running delete operation on %lu, i = %u \n", check_elt, delete_elt, i);
      //     wrong = true;
      //   }
      // } else {
      //   if(!std::count(serial_set.begin(), serial_set.end(), check_elt)) {
      //     printf("checker set, incorrectly deleted %lu after running delete operation on %lu, i = %u \n", check_elt, delete_elt, i);
      //     wrong = true;
      //   }
      // }
      
      // do correctness check for the btree serial

      // if removed, check it's removed from btree. if not, check it exists
      if (was_removed) {
        // might need to change if you get values because exists takes in a key
        if(serial_test_set.exists(check_elt)) {
          printf("serial btree, didn't delete %lu after running delete operation on %lu, i = %u \n", check_elt, delete_elt, i);
          wrong = true;
        }
      } else {
        if(!serial_test_set.exists(check_elt)) {
          printf("serial btree, incorrectly deleted %lu after running delete operation on %lu, i = %u \n", check_elt, delete_elt, i);
          wrong = true;
        }
      }
    }
    */
  // }
#endif

#if CORRECTNESS
  // // Deletion correctness check

  // check that everything in serial set (not erased) is still in btree
  for (auto e : serial_set) {
    if (!serial_test_set.exists(e)) {
      printf("serial btree, incorrectly deleted %lu \n", e);
      wrong = true;
    }
  }
  // check that everything erased from serial set is not in btree
  for (int i = 0; i < elems_removed.size(); i++) {
    if (serial_test_set.exists(elems_removed[i])) {
      printf("serial btree, didn't delete %lu, i = %u \n", elems_removed[i], i);
      wrong = true;
    }
  }

  if (wrong) {
    return {false, 0, 0, 0, 0};
  }

  for (auto e : serial_set) {
    if (!concurrent_set.exists(e)) {
      printf("concurrent btree, incorrectly deleted %lu \n", e);
      wrong = true;
    }
  }
  // check that everything erased from serial set is not in btree
  for (int i = 0; i < elems_removed.size(); i++) {
    if (concurrent_set.exists(elems_removed[i])) {
      printf("concurrent btree, didn't delete %lu, i = %u \n", elems_removed[i], i);
      wrong = true;
    }
  }

  if (wrong) {
    return {false, 0, 0, 0, 0};
  }
#endif

  // return {true, 0, 0, 0, 0};

  // if (serial_set.size() != concurrent_set.size()) {
  //   printf("the sizes of concurrent set and correct set don't match, got %lu, expected %lu\n",
  //          concurrent_set.size(), serial_set.size());
  //   return {false, 0, 0, 0, 0};
  // }

  // for(auto e : indxs_to_remove) {
  //   // might need to change if you get values because exists takes in a key
  //   if(concurrent_set.exists(data[e])) {
  //     printf("concurrent, didn't delete %lu\n", data[e]);
  //     wrong = true;
  //   }
  // }
  // if (wrong) {
  //   return {false, 0, 0, 0, 0};
  // }
// #endif

  return {true, serial_time, parallel_time, serial_remove_time, parallel_remove_time};
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    printf("call with the number of elements to insert\n");
    return -1;
  }
  int trials = 0; // used to be 5
  if (argc > 2) {
    trials = atoi(argv[2]);
  }
  std::seed_seq seed{0};
  int n = atoi(argv[1]);
  std::vector<uint64_t> serial_times;
  std::vector<uint64_t> parallel_times;
  std::vector<uint64_t> serial_remove_times;
  std::vector<uint64_t> parallel_remove_times;
  for (int i = 0; i < trials + 1; i++) {
    auto [correct, serial_insert, parallel_insert, serial_remove, parallel_remove] =
        test_concurrent_btreeset<uint64_t>(n, seed);
    if (!correct) {
      printf("got the wrong answer\n");
      return -1;
    }
    // throw out the first trial
    // if (i > 0) {
    if (true) {
    	serial_times.push_back(serial_insert);
    	parallel_times.push_back(parallel_insert);
      serial_remove_times.push_back(serial_remove);
    	parallel_remove_times.push_back(parallel_remove);
    }
  }
  std::sort(serial_times.begin(), serial_times.end());
  std::sort(parallel_times.begin(), parallel_times.end());
  std::sort(serial_remove_times.begin(), serial_remove_times.end());
  std::sort(parallel_remove_times.begin(), parallel_remove_times.end());
  printf("%lu, %lu, %lu, %lu\n", serial_times[trials / 2], parallel_times[trials / 2],
                                serial_remove_times[trials / 2], parallel_remove_times[trials / 2]);
  return 0;
}
