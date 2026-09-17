cmake_policy(SET CMP0057 NEW)

if(NOT DEFINED JOJO_SOURCE_DIR)
  message(FATAL_ERROR "JOJO_SOURCE_DIR is required")
endif()

if(DEFINED JOJO_READINESS_FILE)
  set(readiness_file "${JOJO_READINESS_FILE}")
else()
  set(readiness_file "${JOJO_SOURCE_DIR}/docs/architecture/PRODUCTION-READINESS.tsv")
endif()

if(NOT EXISTS "${readiness_file}")
  message(FATAL_ERROR "production readiness manifest is missing")
endif()

file(STRINGS "${readiness_file}" readiness_lines)
set(required_ids R2.1 R2.2 R2.3 R2.4 R2.5 R2.6)
set(seen_ids)
set(allowed_statuses not-started implemented-unverified verified blocked-external-evidence)

foreach(line IN LISTS readiness_lines)
  string(STRIP "${line}" line)
  if(line STREQUAL "" OR line MATCHES "^#")
    continue()
  endif()

  string(REPLACE "\t" ";" fields "${line}")
  list(LENGTH fields field_count)
  if(NOT field_count EQUAL 4)
    message(FATAL_ERROR "invalid readiness row: ${line}")
  endif()

  list(GET fields 0 workstream)
  list(GET fields 1 status)
  list(GET fields 2 evidence)
  list(GET fields 3 blocker)

  if(NOT workstream IN_LIST required_ids)
    message(FATAL_ERROR "unknown workstream: ${workstream}")
  endif()
  if(workstream IN_LIST seen_ids)
    message(FATAL_ERROR "duplicate workstream: ${workstream}")
  endif()
  list(APPEND seen_ids "${workstream}")

  if(NOT status IN_LIST allowed_statuses)
    message(FATAL_ERROR "invalid status ${status} for ${workstream}")
  endif()
  if(status STREQUAL "verified" AND evidence STREQUAL "none")
    message(FATAL_ERROR "verified workstream ${workstream} has no evidence")
  endif()
  if(status STREQUAL "verified" AND NOT blocker STREQUAL "none")
    message(FATAL_ERROR "verified workstream ${workstream} still has blocker ${blocker}")
  endif()
endforeach()

foreach(required_id IN LISTS required_ids)
  if(NOT required_id IN_LIST seen_ids)
    message(FATAL_ERROR "missing mandatory workstream: ${required_id}")
  endif()
endforeach()

if(NOT DEFINED JOJO_SKIP_DOC_CHECKS)
  file(READ "${JOJO_SOURCE_DIR}/README.md" readme_text)
  file(READ "${JOJO_SOURCE_DIR}/docs/NEXT-MILESTONES.md" next_text)
  file(READ "${JOJO_SOURCE_DIR}/docs/architecture/PRODUCTION-ROADMAP.md" roadmap_text)

  foreach(required_readme_phrase
      "R2 — Production completion"
      "PRODUCTION-READINESS.tsv"
      "Commercial-game integration is not yet verified")
    string(FIND "${readme_text}" "${required_readme_phrase}" found)
    if(found EQUAL -1)
      message(FATAL_ERROR "README is missing required readiness phrase: ${required_readme_phrase}")
    endif()
  endforeach()

  string(FIND "${next_text}" "R2.1 — Repository truth and release gates" found_next)
  if(found_next EQUAL -1)
    message(FATAL_ERROR "NEXT-MILESTONES does not identify R2.1")
  endif()

  string(FIND "${roadmap_text}" "Production completion program (R2)" found_roadmap)
  if(found_roadmap EQUAL -1)
    message(FATAL_ERROR "roadmap does not reference R2 production completion")
  endif()
endif()
