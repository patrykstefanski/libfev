set(FEV_TESTS
  #sleep
  stress_cond
  stress_cond_with_timeout
  stress_ilock
  stress_mpmc_queue
  stress_mutex
  stress_mutex_with_timeout
  stress_qsbr_queue
  stress_sem
  stress_sem_with_timeout
  stress_thr_mutex
  timers_bucket
)
foreach(target ${FEV_TESTS})
  add_executable(${target} ${target}.c)
  target_include_directories(${target} PRIVATE ../third_party)
  target_link_libraries(${target} PRIVATE fev)
  set_property(TARGET ${target} PROPERTY C_STANDARD 11)
  fev_set_compile_options(${target})
endforeach()
