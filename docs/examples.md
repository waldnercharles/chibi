# Examples

Choose a task below. Each recipe shows the code and the assets it needs.
Put behavior files in `content/behaviors` and prototype files in `content/prototypes`.

## Use a recipe

For the first movement recipe, create these files in an empty project directory.
Save its behavior as `content/behaviors/MovePlayer.chibi`.

`project.kdl`:

```kdl
version 29
name "Movement"
content "content"
start_scene "scenes/Main.kdl"
```

`content/prototypes/Player.kdl`:

```kdl
name "Player"
components {
    Appearance color=0x72ead4ff height=16.0 width=16.0
    (object)Input
    (object)Transform
}
(array)behaviors "MovePlayer"
```

`content/prototypes/View.kdl`:

```kdl
name "View"
components {
    Camera height=360.0 width=640.0
    (object)Transform
}
```

`content/scenes/Main.kdl`:

```kdl
version 1
name "Main"
(array)entities {
    - name="Player" prototype="Player"
    - name="View" prototype="View"
}
```

The scene contains a visible player and a camera.
For another recipe, change the prototype's behavior name and add the components that recipe needs.
To add another prototype, add an entity with its name under `(array)entities`.

## Move with input

Attach this behavior to an entity with `Input` and `Transform` components.
`Move` supplies a direction. `dt` is the time in seconds since the previous tick.

```chibi
MovePlayer :: behavior {
    on Tick :: (dt: s) {
        self.Transform += self.Input.Move * 105 * dt
    }
}
```

This moves the position directly. For a physics character, set `Velocity` instead.
Keep its vertical velocity when changing horizontal movement:

```chibi
MovePlayer :: behavior {
    on Tick :: () {
        self.Velocity = vec2(self.Input.Move.x * 105, self.Velocity.y)
    }
}
```

The second version also needs `Velocity` and a configured physics body.

## React to a button press

Declare an action in `content/input.kdl`:

```kdl
Shoot {
    (array)sources "Space"
}
```

Use `pressed` for one action per press. Use `held` for continuous input.

```chibi
Shoot :: behavior {
    on Input :: () {
        if input.Shoot.pressed {
            print_string("Fire")
        }
    }
}
```

## Move every matching entity

A system selects entities by their components.
This system needs `Transform` and `Velocity` on each matching entity.

```chibi
Drift :: system(position: ^Transform, velocity: Velocity) {
    position^ += velocity * dt
}
```

Save it in `content/systems/Drift.chibi`.
Register it on an entity with the `EngineSystem` component:

```kdl
name "MovementSystem"
components {
    EngineSystem asset="Drift"
}
```

Use this for entities whose position your system controls.
A physics body already integrates velocity, so it does not need this system.

## Spawn an object

Create a `Projectile` prototype with a `Transform` component.
Attach this behavior to an entity that also has `Transform`:

```chibi
FireProjectile :: behavior {
    on Input :: () {
        if input.Shoot.pressed {
            projectile := await spawn_prototype(Projectile)
            projectile.Transform = self.Transform
        }
    }
}
```

The `Shoot` action uses the input declaration from the previous recipe.
`await` waits for the spawn result before the code writes the new entity's position.
Add motion and appearance to the `Projectile` prototype.

## Remove an object after a delay

Store the countdown on the entity. Declare `content/components/Lifetime.chibi`:

```chibi
Lifetime :: component {
    remaining: s
}
```

Create `content/behaviors/Expire.chibi`:

```chibi
Expire :: behavior {
    on Tick :: (dt: s) {
        self.Lifetime.remaining -= dt
        if self.Lifetime.remaining <= 0 {
            destroy_entity(self)
        }
    }
}
```

Give a prototype a two-second lifetime in `content/prototypes/TimedObject.chibi`:

```chibi
TimedObject :: prototype {
    Lifetime { remaining: 2 }
    behaviors: [Expire]
}
```

Each instance has its own countdown. Add visible components to the prototype to see it disappear.

## Check for an obstacle

This ray checks 200 world units to the right.
It selects collision category bit 1, ignores the calling entity, and excludes sensors.
The behavior needs `Transform` and `Velocity` components.

```chibi
AvoidObstacle :: behavior {
    on Tick :: () {
        hit := raycast(
            self.Transform,
            self.Transform + vec2(200, 0),
            1,
            self,
            false
        )
        if hit.Found {
            self.Velocity = self.Velocity * 0.5
        }
    }
}
```

The obstacle needs a collider in the selected category.
Change the endpoint to check a different direction or distance.

## React to a collision

Attach this behavior to a 2D physics body with `Body` and `Collider` components.
It applies an impulse when contact begins:

```chibi
Bounce :: behavior {
    on ContactBegin :: () {
        apply_impulse(self, vec2(0, self.Body.mass * 20))
    }
}
```

The impulse changes velocity once per contact event.
Use `apply_force` during `Tick` for a continuous force.

## Sum a collection

Use an array for a sequence of scores. A `for` loop visits each value.

```chibi
Scores :: behavior {
    TotalScore :: (points: int[]) -> int {
        total: int = 0
        for point in points {
            total += point
        }
        return total
    }

    on Begin :: () {
        points: int[] = [10, 20, 5]
        points.append(15)
        print_string(format_text("Score: {}", TotalScore(points)))
    }
}
```

This prints `Score: 50`. `points.append(15)` adds 15 to the end of `points`.

## Change HUD text

Create `content/ui/Hud.rml` with an element whose ID is `score`:

```html
<span id="score">Score: 0</span>
```

Create a UI prototype in `content/prototypes/Hud.kdl`:

```kdl
name "Hud"
components {
    (object)Transform
    Ui document="Hud"
}
(array)behaviors "ScoreLabel"
```

Define `content/behaviors/ScoreLabel.chibi`:

```chibi
ScoreLabel :: behavior {
    on Begin :: () {
        await set_ui_text(Element: "#score", Text: "Score: 50")
    }
}
```

Place an instance of `Hud` in your scene. The selector `#score` addresses its element.
