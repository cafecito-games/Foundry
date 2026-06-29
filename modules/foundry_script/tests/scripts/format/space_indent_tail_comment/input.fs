func test():
    var a = 1
    ## doc-style tail comment stays in body


func other():
    if a:
        do_thing()
        ## nested tail comment stays in if
    pass
