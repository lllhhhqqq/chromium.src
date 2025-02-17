if (self.importScripts) {
    importScripts('/resources/testharness.js');
    importScripts('/resources/testharness-helpers.js');
    importScripts('../resources/test-helpers.js');
}

prepopulated_cache_test(simple_entries, function(cache, entries) {
    return cache.match('not-present-in-the-cache')
      .then(function(result) {
          assert_equals(result, undefined,
                        'Cache.match failures should resolve with undefined.');
        });
  }, 'Cache.match with no matching entries');

prepopulated_cache_test(simple_entries, function(cache, entries) {
    return cache.match(entries.a.request.url)
      .then(function(result) {
          assert_response_equals(result, entries.a.response,
                                 'Cache.match should match by URL.');
        });
  }, 'Cache.match with URL');

prepopulated_cache_test(simple_entries, function(cache, entries) {
    return cache.match(entries.a.request)
      .then(function(result) {
          assert_response_equals(result, entries.a.response,
                                 'Cache.match should match by Request.');
        });
  }, 'Cache.match with Request');

prepopulated_cache_test(simple_entries, function(cache, entries) {
    var alt_response = new Response('', {status: 201});

    return self.caches.open('second_matching_cache')
      .then(function(cache) {
          return cache.put(entries.a.request, alt_response.clone());
        })
      .then(function() {
          return cache.match(entries.a.request)
        })
      .then(function(result) {
          assert_response_equals(
            result, entries.a.response,
            'Cache.match should match the first cache.');
        });
  }, 'Cache.match with multiple cache hits');

prepopulated_cache_test(simple_entries, function(cache, entries) {
    return cache.match(new Request(entries.a.request.url))
      .then(function(result) {
          assert_response_equals(result, entries.a.response,
                                 'Cache.match should match by Request.');
        });
  }, 'Cache.match with new Request');

prepopulated_cache_test(simple_entries, function(cache, entries) {
    return cache.match(entries.a.request,
                       {ignoreSearch: true})
      .then(function(result) {
          assert_response_in_array(
            result,
            [
              entries.a.response,
              entries.a_with_query.response
            ],
            'Cache.match with ignoreSearch should ignore the ' +
            'search parameters of cached request.');
        });
  },
  'Cache.match with ignoreSearch option (request with no search ' +
  'parameters)');

prepopulated_cache_test(simple_entries, function(cache, entries) {
    return cache.match(entries.a_with_query.request,
                       {ignoreSearch: true})
      .then(function(result) {
          assert_response_in_array(
            result,
            [
              entries.a.response,
              entries.a_with_query.response
            ],
            'Cache.match with ignoreSearch should ignore the ' +
            'search parameters of request.');
        });
  },
  'Cache.match with ignoreSearch option (request with search parameter)');

prepopulated_cache_test(simple_entries, function(cache, entries) {
    return cache.match(entries.cat.request.url + '#mouse')
      .then(function(result) {
          assert_response_equals(result, entries.cat.response,
                                 'Cache.match should ignore URL fragment.');
        });
  }, 'Cache.match with URL containing fragment');

prepopulated_cache_test(simple_entries, function(cache, entries) {
    return cache.match('http')
      .then(function(result) {
          assert_equals(
            result, undefined,
            'Cache.match should treat query as a URL and not ' +
            'just a string fragment.');
        });
  }, 'Cache.match with string fragment "http" as query');

prepopulated_cache_test(vary_entries, function(cache, entries) {
    return cache.match('http://example.com/c')
      .then(function(result) {
          assert_response_in_array(
            result,
            [
              entries.vary_cookie_absent.response
            ],
            'Cache.match should honor "Vary" header.');
        });
  }, 'Cache.match with responses containing "Vary" header');

cache_test(function(cache) {
    var request = new Request('http://example.com');
    var response;
    var request_url = new URL('../resources/simple.txt', location.href).href;
    return fetch(request_url)
      .then(function(fetch_result) {
          response = fetch_result;
          assert_equals(
            response.url, request_url,
            '[https://fetch.spec.whatwg.org/#dom-response-url] ' +
            'Reponse.url should return the URL of the response.');
          return cache.put(request, response.clone());
        })
      .then(function() {
          return cache.match(request.url);
        })
      .then(function(result) {
          assert_response_equals(
            result, response,
            'Cache.match should return a Response object that has the same ' +
            'properties as the stored response.');
          return cache.match(response.url);
        })
      .then(function(result) {
          assert_equals(
            result, undefined,
            'Cache.match should not match cache entry based on response URL.');
        });
  }, 'Cache.match with Request and Response objects with different URLs');

cache_test(function(cache) {
    var request_url = new URL('../resources/simple.txt', location.href).href;
    return fetch(request_url)
      .then(function(fetch_result) {
          return cache.put(new Request(request_url), fetch_result);
        })
      .then(function() {
          return cache.match(request_url);
        })
      .then(function(result) {
          return result.text();
        })
      .then(function(body_text) {
          assert_equals(body_text, 'a simple text file\n',
                        'Cache.match should return a Response object with a ' +
                        'valid body.');
        })
      .then(function() {
          return cache.match(request_url);
        })
      .then(function(result) {
          return result.text();
        })
      .then(function(body_text) {
          assert_equals(body_text, 'a simple text file\n',
                        'Cache.match should return a Response object with a ' +
                        'valid body each time it is called.');
        });
  }, 'Cache.match invoked multiple times for the same Request/Response');

cache_test(function(cache) {
    var request_url = new URL('../resources/simple.txt', location.href).href;
    return fetch(request_url)
      .then(function(fetch_result) {
          return cache.put(new Request(request_url), fetch_result);
        })
      .then(function() {
          return cache.match(request_url);
        })
      .then(function(result) {
          return result.blob();
        })
      .then(function(blob) {
          sliced = blob.slice(2,8);

          return new Promise(function (resolve, reject) {
              reader = new FileReader();
              reader.onloadend = function(event) {
                resolve(event.target.result);
              }
              reader.readAsText(sliced);
            });
        })
      .then(function(text) {
          assert_equals(text, 'simple',
                        'A Response blob returned by Cache.match should be ' +
                        'sliceable.' );
        });
  }, 'Cache.match blob should be sliceable');

prepopulated_cache_test(simple_entries, function(cache, entries) {
    var request = new Request(entries.a.request.clone(), {method: 'POST'});
    return cache.match(request)
      .then(function(result) {
          assert_equals(result, undefined,
                        'Cache.match should not find a match');
        });
  }, 'Cache.match with POST Request');

prepopulated_cache_test(simple_entries, function(cache, entries) {
    var response = entries.non_2xx_response.response;
    return cache.match(entries.non_2xx_response.request.url)
      .then(function(result) {
          assert_response_equals(
              result, entries.non_2xx_response.response,
              'Cache.match should return a Response object that has the ' +
                  'same properties as a stored non-2xx response.');
        });
  }, 'Cache.match with a non-2xx Response');

prepopulated_cache_test(simple_entries, function(cache, entries) {
    var response = entries.error_response.response;
    return cache.match(entries.error_response.request.url)
      .then(function(result) {
          assert_response_equals(
              result, entries.error_response.response,
              'Cache.match should return a Response object that has the ' +
                  'same properties as a stored network error response.');
        });
  }, 'Cache.match with a network error Response');

done();
