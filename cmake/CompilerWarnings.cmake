# 引擎统一编译策略：无异常、无 RTTI、严格告警
function(nq_engine_flags target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /EHs-c- /GR-)
    if(NQ_WERROR)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic
      -fno-exceptions -fno-rtti
      -fvisibility=hidden
    )
    if(NQ_WERROR)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()
